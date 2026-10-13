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
    $plannerOutput = @(& $EndpointManagerPath 'probe-plan' "$FleetTimeoutMilliseconds" '--json' 2>&1 |
        ForEach-Object { [string]$_ })
    $managerExitCode = $LASTEXITCODE
    $plannerText = $plannerOutput -join "`n"
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
        $plan.instanceCount -gt 2 -or $plan.busCount -ne $plan.buses.Count -or
        $plan.buses.Count -ne ($plan.instanceCount * 2)) {
        throw 'Probe plan does not describe one or two stable adapters with exactly two buses each.'
    }

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
    & $InteractiveLauncherPath @launchArguments | Out-Null
} catch {
    $failure = $_.Exception.Message
}

if (Test-Path -LiteralPath $probeLogPath -PathType Leaf) {
    $probeOutput = @(Get-Content -LiteralPath $probeLogPath)
    $exitLines = @($probeOutput | Where-Object { [string]$_ -match '^PROBE_EXIT=(\d+)$' })
    if ($exitLines.Count -eq 1) {
        $probeExitCode = [int]([string]$exitLines[0] -replace '^PROBE_EXIT=', '')
    } elseif ($failure -eq $null) {
        $failure = 'Interactive audio probe report did not contain exactly one exit status.'
    }
    if ($probeExitCode -ne $null -and $probeExitCode -ne 0 -and $failure -eq $null) {
        $failure = "WASAPI audio probe exited with code $probeExitCode."
    }
} elseif ($failure -eq $null) {
    $failure = 'Interactive audio probe did not produce a report.'
}

$report = [ordered]@{
    Passed = ($failure -eq $null -and $managerExitCode -eq 0 -and $probeExitCode -eq 0)
    StartedAtUtc = $startedAt.ToString('o')
    FinishedAtUtc = [DateTimeOffset]::UtcNow.ToString('o')
    DurationSeconds = $DurationSeconds
    MaximumLevelSpreadDb = $MaximumLevelSpreadDb
    ManagerExitCode = $managerExitCode
    Plan = $plan
    PlannerOutput = $plannerOutput
    ProbeExitCode = $probeExitCode
    ProbeOutput = $probeOutput
    Error = $failure
}
$json = $report | ConvertTo-Json -Depth 12
Set-Content -LiteralPath $reportPath -Value $json -Encoding UTF8
Write-Output $json
if (-not $report.Passed) {
    throw "Fleet audio probe failed: $failure. Evidence: $reportPath"
}
