[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $EndpointManagerPath,
    [Parameter(Mandatory)] [string] $ProbePath,
    [ValidateRange(5, 3600)] [int] $DurationSeconds = 60,
    [ValidateRange(1000, 120000)] [int] $FleetTimeoutMilliseconds = 30000,
    [ValidateRange(0, 60)] [double] $MaximumLevelSpreadDb = 0,
    [Parameter(Mandatory)] [string] $OutputPath,
    [string] $InteractiveLauncherPath = (Join-Path $PSScriptRoot 'lab-interactive-probe.ps1')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($MaximumLevelSpreadDb -gt 0 -and $MaximumLevelSpreadDb -lt 0.1) {
    throw 'MaximumLevelSpreadDb must be zero or between 0.1 and 60 dB.'
}
foreach ($path in @($EndpointManagerPath, $ProbePath, $InteractiveLauncherPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required executable or script not found: $path"
    }
}

$reportPath = [IO.Path]::GetFullPath($OutputPath)
$maximumProbeReportBytes = 16MB
$probeLogPath = "$reportPath.probe.txt"
$reportDirectory = Split-Path -Parent $reportPath
if ($reportDirectory -and -not (Test-Path -LiteralPath $reportDirectory)) {
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
}
Remove-Item -LiteralPath $reportPath, $probeLogPath -Force -ErrorAction SilentlyContinue

$startedAt = [DateTimeOffset]::UtcNow
$plannerOutput = @()
$probeOutput = @()
$plan = $null
$probeExitCode = $null
$failure = $null
$managerExitCode = $null

try {
    Write-Verbose 'Waiting for a stable SAR adapter fleet and its probe plan.'
    $plannerOutput = [string[]]@(& $EndpointManagerPath 'probe-plan' "$FleetTimeoutMilliseconds" '--json' 2>&1 |
        ForEach-Object { [string]$_ })
    $managerExitCode = $LASTEXITCODE
    $plannerText = $plannerOutput -join "`n"
    Write-Verbose "Endpoint manager returned exit code $managerExitCode; parsing probe plan."
    try {
        $plan = $plannerText | ConvertFrom-Json
    } catch {
        throw "Endpoint manager returned invalid probe-plan JSON (exit $managerExitCode): $plannerText"
    }
    if ($managerExitCode -ne 0) {
        throw "Fleet did not reach stable readiness (exit $managerExitCode): $($plan.error)"
    }
    if ($plan.state -ne 'ready' -or -not $plan.ready -or
        $plan.stableSamples -lt 3 -or $plan.instanceCount -lt 1 -or
        $plan.instanceCount -gt 4 -or $plan.busCount -ne $plan.buses.Count -or
        $plan.buses.Count -ne ($plan.instanceCount * 2)) {
        throw 'Probe plan does not describe one to four stable adapters with exactly two buses each.'
    }
    Write-Verbose "Validated $($plan.instanceCount) adapters and $($plan.buses.Count) stereo buses."

    $seenIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    for ($index = 0; $index -lt $plan.buses.Count; $index++) {
        $bus = $plan.buses[$index]
        if ($bus.fleetBus -ne $index -or $bus.instanceBus -ne ($index % 2) -or
            [string]::IsNullOrWhiteSpace($bus.instanceId)) {
            throw "Probe plan bus ordering or instance identity is invalid at index $index."
        }
        if ($bus.renderEndpointId -notmatch '^\{0\.0\.0\.00000000\}\.\{[0-9a-fA-F-]{36}\}$' -or
            $bus.captureEndpointId -notmatch '^\{0\.0\.1\.00000000\}\.\{[0-9a-fA-F-]{36}\}$') {
            throw "Probe plan bus $index contains an endpoint with the wrong WASAPI flow."
        }
        if (-not $seenIds.Add($bus.renderEndpointId) -or
            -not $seenIds.Add($bus.captureEndpointId)) {
            throw "Probe plan reuses a WASAPI endpoint ID at bus $index."
        }
    }

    $launchArguments = @{
        ProbePath = $ProbePath
        RenderId = [string]$plan.buses[0].renderEndpointId
        CaptureId = [string]$plan.buses[0].captureEndpointId
        Mode = 'multi_bus'
        DurationSeconds = $DurationSeconds
        MaximumLevelSpreadDb = $MaximumLevelSpreadDb
        OutputPath = $probeLogPath
    }
    if ($plan.buses.Count -gt 1) {
        $launchArguments.AdditionalRenderIds = @($plan.buses | Select-Object -Skip 1 |
            ForEach-Object { [string]$_.renderEndpointId })
        $launchArguments.AdditionalCaptureIds = @($plan.buses | Select-Object -Skip 1 |
            ForEach-Object { [string]$_.captureEndpointId })
    }
    Write-Verbose "Starting interactive WASAPI measurement for $($plan.buses.Count) buses."
    & $InteractiveLauncherPath @launchArguments | Out-Null
    Write-Verbose 'Interactive WASAPI measurement returned.'
} catch {
    $failure = $_.Exception.Message
}

Write-Verbose 'Evaluating the interactive probe report.'
if (Test-Path -LiteralPath $probeLogPath -PathType Leaf) {
    $probeReportBytes = (Get-Item -LiteralPath $probeLogPath).Length
    if ($probeReportBytes -gt $maximumProbeReportBytes) {
        $failure = "Interactive probe report exceeds the $maximumProbeReportBytes-byte limit."
    } else {
        $probeOutput = [string[]]@(Get-Content -LiteralPath $probeLogPath |
            ForEach-Object { [string]$_ })
        Write-Verbose "Read $($probeOutput.Count) probe report lines."
        $exitLines = @($probeOutput | Where-Object { [string]$_ -match '^PROBE_EXIT=(\d+)$' })
        if ($exitLines.Count -eq 1) {
            $probeExitCode = [int]([string]$exitLines[0] -replace '^PROBE_EXIT=', '')
        } elseif ($failure -eq $null) {
            $failure = 'Interactive audio probe report did not contain exactly one exit status.'
        }
        if ($probeExitCode -ne $null -and $probeExitCode -ne 0 -and $failure -eq $null) {
            $failure = "WASAPI audio probe exited with code $probeExitCode."
        }
    }
} elseif ($failure -eq $null) {
    $failure = 'Interactive audio probe did not produce a report.'
}

Write-Verbose 'Assembling fleet report snapshot.'
$planSnapshot = $null
if ($null -ne $plan) {
    $planBuses = @(
        foreach ($bus in $plan.buses) {
            [ordered]@{
                instanceId = [string]$bus.instanceId
                instanceLabel = [string]$bus.instanceLabel
                fleetBus = [int]$bus.fleetBus
                instanceBus = [int]$bus.instanceBus
                renderEndpointId = [string]$bus.renderEndpointId
                captureEndpointId = [string]$bus.captureEndpointId
            }
        }
    )
    $planSnapshot = [ordered]@{
        state = [string]$plan.state
        ready = [bool]$plan.ready
        stableSamples = [int]$plan.stableSamples
        instanceCount = [int]$plan.instanceCount
        busCount = [int]$plan.busCount
        error = [string]$plan.error
        buses = $planBuses
    }
}
$report = [ordered]@{
    Passed = ($failure -eq $null -and $managerExitCode -eq 0 -and $probeExitCode -eq 0)
    StartedAtUtc = $startedAt.ToString('o')
    FinishedAtUtc = [DateTimeOffset]::UtcNow.ToString('o')
    DurationSeconds = $DurationSeconds
    MaximumLevelSpreadDb = $MaximumLevelSpreadDb
    ManagerExitCode = $managerExitCode
    Plan = $planSnapshot
    PlannerOutput = $plannerOutput
    ProbeExitCode = $probeExitCode
    ProbeOutput = $probeOutput
    Error = $failure
}
Write-Verbose 'Serializing fleet report snapshot.'
$json = ConvertTo-Json -InputObject $report -Depth 12
Write-Verbose "Serialized fleet report to $($json.Length) characters."
Write-Verbose 'Writing the machine-readable fleet report.'
Set-Content -LiteralPath $reportPath -Value $json -Encoding UTF8
Write-Output $json
if (-not $report.Passed) {
    throw "Fleet audio probe failed: $failure. Evidence: $reportPath"
}
