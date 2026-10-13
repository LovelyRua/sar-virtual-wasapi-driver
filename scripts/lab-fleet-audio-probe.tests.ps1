$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'lab-fleet-audio-probe.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "sar-fleet-audio-$([guid]::NewGuid().ToString('N'))"
$managerPath = Join-Path $root 'manager.cmd'
$launcherPath = Join-Path $root 'fake-launcher.ps1'
$probePath = Join-Path $root 'probe.exe'
$outputPath = Join-Path $root 'report.json'
$argsPath = Join-Path $root 'launcher-args.txt'
$renderIds = @(
    '{0.0.0.00000000}.{11111111-1111-1111-1111-111111111111}',
    '{0.0.0.00000000}.{22222222-2222-2222-2222-222222222222}',
    '{0.0.0.00000000}.{33333333-3333-3333-3333-333333333333}',
    '{0.0.0.00000000}.{44444444-4444-4444-4444-444444444444}'
)
$captureIds = @(
    '{0.0.1.00000000}.{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}',
    '{0.0.1.00000000}.{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}',
    '{0.0.1.00000000}.{cccccccc-cccc-cccc-cccc-cccccccccccc}',
    '{0.0.1.00000000}.{dddddddd-dddd-dddd-dddd-dddddddddddd}'
)
$savedEnvironment = @{}
foreach ($name in 'SAR_FLEET_TEST_PLAN', 'SAR_FLEET_TEST_MANAGER_EXIT',
                  'SAR_FLEET_TEST_PROBE_EXIT', 'SAR_FLEET_TEST_ARGS_PATH') {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

function New-TestPlan([bool] $Ready = $true, [int] $InstanceCount = 2) {
    $buses = @()
    $busCount = $InstanceCount * 2
    for ($index = 0; $index -lt $busCount; $index++) {
        $buses += [pscustomobject]@{
            instanceId = if ($index -lt 2) { 'ROOT\MEDIA\0001' } else { 'ROOT\MEDIA\0002' }
            instanceLabel = if ($index -lt 2) { 'Test A' } else { 'Test B' }
            fleetBus = $index
            instanceBus = ($index % 2)
            renderEndpointId = $renderIds[$index]
            captureEndpointId = $captureIds[$index]
        }
    }
    [pscustomobject]@{
        state = if ($Ready) { 'ready' } else { 'not-ready' }
        ready = $Ready
        stableSamples = if ($Ready) { 3 } else { 0 }
        instanceCount = $InstanceCount
        busCount = $buses.Count
        error = if ($Ready) { '' } else { 'synthetic not-ready fleet' }
        buses = $buses
    }
}

function Invoke-TestRunner {
    & $runner -EndpointManagerPath $managerPath -ProbePath $probePath `
        -InteractiveLauncherPath $launcherPath -DurationSeconds 5 `
        -FleetTimeoutMilliseconds 5000 -MaximumLevelSpreadDb 1.0 `
        -OutputPath $outputPath | Out-Null
}

function Assert-Failure([string] $ExpectedMessage) {
    $caught = $false
    try {
        Invoke-TestRunner
    } catch {
        $caught = $true
        if ($_.Exception.Message -notmatch [regex]::Escape($ExpectedMessage)) {
            throw "Unexpected runner result; expected '$ExpectedMessage': $_"
        }
    }
    if (-not $caught) {
        throw "Expected runner failure containing '$ExpectedMessage'."
    }
}

New-Item -ItemType Directory -Path $root | Out-Null
Set-Content -LiteralPath $probePath -Value 'placeholder'
Set-Content -LiteralPath $managerPath -Encoding ASCII -Value @'
@echo off
echo %SAR_FLEET_TEST_PLAN%
exit /b %SAR_FLEET_TEST_MANAGER_EXIT%
'@
Set-Content -LiteralPath $launcherPath -Encoding UTF8 -Value @'
param(
    [string]$ProbePath, [string]$RenderId, [string]$CaptureId, [string]$Mode,
    [int]$DurationSeconds, [double]$MaximumLevelSpreadDb, [string]$OutputPath,
    [string[]]$AdditionalRenderIds = @(), [string[]]$AdditionalCaptureIds = @()
)
@(
    "Mode=$Mode",
    "RenderId=$RenderId",
    "CaptureId=$CaptureId",
    "AdditionalRenderIds=$($AdditionalRenderIds -join ',')",
    "AdditionalCaptureIds=$($AdditionalCaptureIds -join ',')",
    "MaximumLevelSpreadDb=$MaximumLevelSpreadDb"
) | Set-Content -LiteralPath $env:SAR_FLEET_TEST_ARGS_PATH
Set-Content -LiteralPath $OutputPath -Value @('synthetic audio analysis', "PROBE_EXIT=$env:SAR_FLEET_TEST_PROBE_EXIT")
'@

try {
    $env:SAR_FLEET_TEST_ARGS_PATH = $argsPath
    $env:SAR_FLEET_TEST_MANAGER_EXIT = '0'
    $env:SAR_FLEET_TEST_PROBE_EXIT = '0'
    $env:SAR_FLEET_TEST_PLAN = (New-TestPlan | ConvertTo-Json -Depth 8 -Compress)
    Invoke-TestRunner
    $report = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    $launcherArgs = Get-Content -LiteralPath $argsPath -Raw
    if (-not $report.Passed -or $report.Plan.busCount -ne 4 -or
        $report.ProbeExitCode -ne 0 -or
        $launcherArgs -notmatch 'Mode=multi_bus' -or
        $launcherArgs -notmatch [regex]::Escape("AdditionalRenderIds=$($renderIds[1]),$($renderIds[2]),$($renderIds[3])") -or
        $launcherArgs -notmatch [regex]::Escape("AdditionalCaptureIds=$($captureIds[1]),$($captureIds[2]),$($captureIds[3])")) {
        throw "Four-bus run was not passed to the interactive probe correctly: $launcherArgs"
    }

    $env:SAR_FLEET_TEST_PLAN = (New-TestPlan -InstanceCount 1 | ConvertTo-Json -Depth 8 -Compress)
    Invoke-TestRunner
    $report = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    $launcherArgs = Get-Content -LiteralPath $argsPath -Raw
    if (-not $report.Passed -or $report.Plan.busCount -ne 2 -or
        $launcherArgs -notmatch [regex]::Escape("AdditionalRenderIds=$($renderIds[1])") -or
        $launcherArgs -notmatch [regex]::Escape("AdditionalCaptureIds=$($captureIds[1])")) {
        throw "Two-bus single-adapter run was not planned correctly: $launcherArgs"
    }

    $env:SAR_FLEET_TEST_PROBE_EXIT = '17'
    Assert-Failure 'WASAPI audio probe exited with code 17.'
    $report = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    if ($report.Passed -or $report.ProbeExitCode -ne 17) {
        throw 'Nonzero interactive probe status was incorrectly accepted.'
    }

    $env:SAR_FLEET_TEST_PROBE_EXIT = '0'
    $env:SAR_FLEET_TEST_MANAGER_EXIT = '3'
    Assert-Failure 'Fleet did not reach stable readiness'
    $report = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    if ($report.Passed -or $report.ManagerExitCode -ne 3) {
        throw 'Unstable fleet readiness was incorrectly accepted.'
    }

    $env:SAR_FLEET_TEST_MANAGER_EXIT = '0'
    $invalidPlan = New-TestPlan
    $invalidPlan.buses[3].captureEndpointId = $invalidPlan.buses[2].captureEndpointId
    $env:SAR_FLEET_TEST_PLAN = $invalidPlan | ConvertTo-Json -Depth 8 -Compress
    Remove-Item -LiteralPath $argsPath -Force -ErrorAction SilentlyContinue
    Assert-Failure 'reuses a WASAPI endpoint ID'
    $report = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    if ($report.Passed -or (Test-Path -LiteralPath $argsPath)) {
        throw 'Invalid duplicate-endpoint plan reached the interactive launcher.'
    }

    Write-Output 'lab_fleet_audio_probe_tests passed=5'
} finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
exit 0
