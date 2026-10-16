$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'lab-interactive-probe.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "sar-interactive-probe-$([guid]::NewGuid().ToString('N'))"
$probePath = Join-Path $root 'probe.exe'
$reportPath = Join-Path $root 'report.txt'
$validRender = '{0.0.0.00000000}.{11111111-1111-1111-1111-111111111111}'
$validCapture = '{0.0.1.00000000}.{22222222-2222-2222-2222-222222222222}'
$validRender2 = '{0.0.0.00000000}.{33333333-3333-3333-3333-333333333333}'
$validCapture2 = '{0.0.1.00000000}.{44444444-4444-4444-4444-444444444444}'
$validRender3 = '{0.0.0.00000000}.{55555555-5555-5555-5555-555555555555}'
$validCapture3 = '{0.0.1.00000000}.{66666666-6666-6666-6666-666666666666}'
$validRender4 = '{0.0.0.00000000}.{77777777-7777-7777-7777-777777777777}'
$validCapture4 = '{0.0.1.00000000}.{88888888-8888-8888-8888-888888888888}'
$validRender5 = '{0.0.0.00000000}.{99999999-9999-9999-9999-999999999999}'
$validCapture5 = '{0.0.1.00000000}.{99999999-9999-9999-9999-999999999999}'
$validRender6 = '{0.0.0.00000000}.{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}'
$validCapture6 = '{0.0.1.00000000}.{aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa}'
$validRender7 = '{0.0.0.00000000}.{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}'
$validCapture7 = '{0.0.1.00000000}.{bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb}'
$validRender8 = '{0.0.0.00000000}.{cccccccc-cccc-cccc-cccc-cccccccccccc}'
$validCapture8 = '{0.0.1.00000000}.{cccccccc-cccc-cccc-cccc-cccccccccccc}'

New-Item -ItemType Directory -Path $root | Out-Null
Set-Content -LiteralPath $probePath -Value 'placeholder'

function Assert-Rejected([hashtable] $Arguments, [string] $ExpectedMessage) {
    try {
        & $scriptPath @Arguments
        throw "Expected validation failure: $ExpectedMessage"
    } catch {
        if ($_.Exception.Message -notmatch [regex]::Escape($ExpectedMessage)) {
            throw "Unexpected validation result; expected '$ExpectedMessage': $_"
        }
    }
}

try {
    $global:SarCapturedActionArgument = $null
    $global:SarTaskDisappears = $false
    $global:SarFakeProbeStatus = 'PROBE_EXIT=0'
    $global:SarKeepTaskRunning = $false
    $global:SarStopRequested = $false
    $global:SarStopCalls = 0
    $global:SarUnregisterCalls = 0
    $global:SarForceTimeout = $false
    $global:SarGetDateCalls = 0
    $global:SarFakeNow = [datetime]'2026-01-01T00:00:00'
    function New-ScheduledTaskAction {
        param([string] $Execute, [string] $Argument)
        [pscustomobject]@{ Execute = $Execute; Argument = $Argument }
    }
    function New-ScheduledTaskPrincipal {
        param([string] $UserId, [string] $LogonType, [string] $RunLevel)
        [pscustomobject]@{ UserId = $UserId; LogonType = $LogonType; RunLevel = $RunLevel }
    }
    function New-ScheduledTaskSettingsSet {
        param([timespan] $ExecutionTimeLimit)
        [pscustomobject]@{ ExecutionTimeLimit = $ExecutionTimeLimit }
    }
    function Register-ScheduledTask {
        param($TaskName, $Action, $Principal, $Settings, [switch] $Force)
        $global:SarCapturedActionArgument = $Action.Argument
    }
    function Start-ScheduledTask {
        if (-not $global:SarKeepTaskRunning) {
            Set-Content -LiteralPath $reportPath -Value @('synthetic probe output', $global:SarFakeProbeStatus)
        }
    }
    function Get-ScheduledTask {
        [CmdletBinding()]
        param([string] $TaskName)
        if ($global:SarTaskDisappears) { return $null }
        $state = if ($global:SarKeepTaskRunning -and -not $global:SarStopRequested) { 'Running' } else { 'Ready' }
        [pscustomobject]@{ State = $state }
    }
    function Stop-ScheduledTask {
        [CmdletBinding()]
        param([string] $TaskName)
        $global:SarStopCalls++
        $global:SarStopRequested = $true
    }
    function Unregister-ScheduledTask {
        [CmdletBinding()]
        param([string] $TaskName, [switch] $Confirm)
        $global:SarUnregisterCalls++
    }
    function Get-Date {
        $global:SarGetDateCalls++
        if ($global:SarForceTimeout -and $global:SarGetDateCalls -eq 2) {
            $global:SarFakeNow = $global:SarFakeNow.AddMinutes(1)
        }
        $global:SarFakeNow
    }
    function Get-Process { [pscustomobject]@{ SessionId = 1 } }

    $base = @{
        ProbePath = $probePath
        RenderId = $validRender
        CaptureId = $validCapture
        OutputPath = $reportPath
    }
    $missingPair = $base.Clone()
    $missingPair.Mode = 'dual_bus'
    Assert-Rejected $missingPair 'dual_bus mode requires RenderId2 and CaptureId2.'

    $global:SarTaskDisappears = $true
    $completedAfterTaskRemoval = @(& $scriptPath @base)
    $global:SarTaskDisappears = $false
    if ($completedAfterTaskRemoval -notcontains 'PROBE_EXIT=0') {
        throw 'Launcher did not accept a completed report after the scheduled task disappeared.'
    }

    $global:SarKeepTaskRunning = $true
    $global:SarForceTimeout = $true
    $global:SarStopRequested = $false
    $global:SarStopCalls = 0
    $global:SarFakeNow = [datetime]'2026-01-01T00:00:00'
    $global:SarGetDateCalls = 0
    $unregistersBeforeTimeout = $global:SarUnregisterCalls
    Assert-Rejected $base 'Interactive probe timed out.'
    if ($global:SarStopCalls -ne 1 -or -not $global:SarStopRequested) {
        throw 'Timed-out interactive probe task was not stopped.'
    }
    if ($global:SarUnregisterCalls -ne ($unregistersBeforeTimeout + 1)) {
        throw 'Timed-out interactive probe task was not unregistered.'
    }
    $global:SarKeepTaskRunning = $false
    $global:SarForceTimeout = $false

    $global:SarFakeProbeStatus = 'PROBE_EXIT=7'
    Assert-Rejected $base 'WASAPI probe failed with exit code 7.'
    $global:SarFakeProbeStatus = 'missing-status'
    Assert-Rejected $base 'Interactive probe report must contain exactly one PROBE_EXIT status line.'
    $global:SarFakeProbeStatus = @('PROBE_EXIT=0', 'PROBE_EXIT=1')
    Assert-Rejected $base 'Interactive probe report must contain exactly one PROBE_EXIT status line.'
    $global:SarFakeProbeStatus = 'PROBE_EXIT=0'

    $singleWithExtra = $base.Clone()
    $singleWithExtra.RenderId2 = $validRender2
    $singleWithExtra.CaptureId2 = $validCapture2
    Assert-Rejected $singleWithExtra 'RenderId2 and CaptureId2 are only valid in dual_bus mode.'

    $invalidEndpoint = $base.Clone()
    $invalidEndpoint.Mode = 'dual_bus'
    $invalidEndpoint.RenderId2 = 'not-a-wasapi-id'
    $invalidEndpoint.CaptureId2 = $validCapture2
    Assert-Rejected $invalidEndpoint 'Invalid WASAPI endpoint ID:'

    $wrongFlow = $base.Clone()
    $wrongFlow.Mode = 'dual_bus'
    $wrongFlow.RenderId2 = $validRender2
    $wrongFlow.CaptureId2 = $validRender3
    Assert-Rejected $wrongFlow 'requires a capture endpoint at position 3.'

    $duplicateEndpoint = $base.Clone()
    $duplicateEndpoint.Mode = 'dual_bus'
    $duplicateEndpoint.RenderId2 = $validRender
    $duplicateEndpoint.CaptureId2 = $validCapture2
    Assert-Rejected $duplicateEndpoint 'Endpoint IDs must be unique across the probe:'

    $oneExtra = $base.Clone()
    $oneExtra.Mode = 'multi_bus'
    Assert-Rejected $oneExtra 'Multi-bus mode requires two to eight endpoint pairs.'

    $mismatched = $base.Clone()
    $mismatched.Mode = 'multi_bus'
    $mismatched.AdditionalRenderIds = @($validRender2, $validRender3)
    $mismatched.AdditionalCaptureIds = @($validCapture2)
    Assert-Rejected $mismatched 'Multi-bus mode requires two to eight endpoint pairs.'

    $tooMany = $base.Clone()
    $tooMany.Mode = 'multi_bus'
    $tooMany.AdditionalRenderIds = @($validRender2, $validRender3, $validRender4, $validRender5, $validRender6, $validRender7, $validRender8, $validRender)
    $tooMany.AdditionalCaptureIds = @($validCapture2, $validCapture3, $validCapture4, $validCapture5, $validCapture6, $validCapture7, $validCapture8, $validCapture)
    Assert-Rejected $tooMany 'Multi-bus mode requires two to eight endpoint pairs.'

    $tooSmallLevelLimit = $base.Clone()
    $tooSmallLevelLimit.MaximumLevelSpreadDb = 0.09
    Assert-Rejected $tooSmallLevelLimit 'MaximumLevelSpreadDb must be zero or between 0.1 and 60 dB.'

    $invalidMmcssMode = $base.Clone()
    $invalidMmcssMode.DisableMmcss = $true
    Assert-Rejected $invalidMmcssMode 'DisableMmcss is only valid for single_bus, dual_bus, and multi_bus probe modes.'

    $tooLargeLevelLimit = $base.Clone()
    $tooLargeLevelLimit.MaximumLevelSpreadDb = 60.1
    try {
        & $scriptPath @tooLargeLevelLimit | Out-Null
        throw 'Expected MaximumLevelSpreadDb validation failure.'
    } catch {
        if ($_.Exception.Message -notmatch 'MaximumLevelSpreadDb') { throw }
    }

    $badEighthEndpoint = $base.Clone()
    $badEighthEndpoint.Mode = 'multi_bus'
    $badEighthEndpoint.AdditionalRenderIds = @($validRender2, $validRender3, $validRender4, $validRender5, $validRender6, $validRender7, 'bad-endpoint')
    $badEighthEndpoint.AdditionalCaptureIds = @($validCapture2, $validCapture3, $validCapture4, $validCapture5, $validCapture6, $validCapture7, $validCapture8)
    Assert-Rejected $badEighthEndpoint 'Invalid WASAPI endpoint ID:'

    $legacyPlusMulti = $base.Clone()
    $legacyPlusMulti.Mode = 'multi_bus'
    $legacyPlusMulti.RenderId2 = $validRender2
    $legacyPlusMulti.CaptureId2 = $validCapture2
    $legacyPlusMulti.AdditionalRenderIds = @($validRender3)
    $legacyPlusMulti.AdditionalCaptureIds = @($validCapture3)
    Assert-Rejected $legacyPlusMulti 'RenderId2 and CaptureId2 cannot be combined with multi_bus mode.'

    $multi = $base.Clone()
    $multi.Mode = 'multi_bus'
    $multi.AdditionalRenderIds = @($validRender2, $validRender3, $validRender4, $validRender5, $validRender6, $validRender7, $validRender8)
    $multi.AdditionalCaptureIds = @($validCapture2, $validCapture3, $validCapture4, $validCapture5, $validCapture6, $validCapture7, $validCapture8)
    & $scriptPath @multi | Out-Null
    $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
    $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    $allMultiIds = @($validRender, $validCapture, $validRender2, $validCapture2, $validRender3, $validCapture3, $validRender4, $validCapture4, $validRender5, $validCapture5, $validRender6, $validCapture6, $validRender7, $validCapture7, $validRender8, $validCapture8)
    $expectedMultiArguments = '--multi 15 ' + (($allMultiIds | ForEach-Object { "'$_'" }) -join ' ')
    if (-not $command.Contains($expectedMultiArguments)) {
        throw "Eight-bus launcher generated an unexpected command: $command"
    }

    $multiWithLevelLimit = $multi.Clone()
    $multiWithLevelLimit.MaximumLevelSpreadDb = 6.5
    & $scriptPath @multiWithLevelLimit | Out-Null
    $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
    $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    if (-not $command.Contains("$expectedMultiArguments --max-level-spread-db 6.5")) {
        throw "Multi-bus launcher did not forward the level-spread limit: $command"
    }

    $multiWithoutMmcss = $multi.Clone()
    $multiWithoutMmcss.DisableMmcss = $true
    $multiWithoutMmcss.MaximumLevelSpreadDb = 1.0
    & $scriptPath @multiWithoutMmcss | Out-Null
    $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
    $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    if (-not $command.Contains("$expectedMultiArguments --max-level-spread-db 1 --no-mmcss")) {
        throw "Multi-bus launcher did not forward the diagnostic MMCSS control switch: $command"
    }

    $single = $base.Clone()
    $single.Mode = 'single_bus'
    $single.BusIndex = 2
    & $scriptPath @single | Out-Null
    $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
    $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    $expectedSingleArguments = "--single 2 '$validRender' '$validCapture' 15"
    if (-not $command.Contains($expectedSingleArguments)) {
        throw "Single-bus launcher generated an unexpected command: $command"
    }

    $singleWithLevelLimit = $single.Clone()
    $singleWithLevelLimit.MaximumLevelSpreadDb = 1.0
    & $scriptPath @singleWithLevelLimit | Out-Null
    $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
    $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    if (-not $command.Contains("$expectedSingleArguments --max-level-spread-db 1")) {
        throw "Single-bus launcher did not forward the level-spread limit: $command"
    }

    $singleWithoutMmcss = $single.Clone()
    $singleWithoutMmcss.DisableMmcss = $true
    & $scriptPath @singleWithoutMmcss | Out-Null
    $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
    $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    if (-not $command.Contains("$expectedSingleArguments --no-mmcss")) {
        throw "Single-bus launcher did not forward the diagnostic MMCSS control switch: $command"
    }

    foreach ($boundaryBus in 0, 7) {
        $boundary = $base.Clone()
        $boundary.Mode = 'single_bus'
        $boundary.BusIndex = $boundaryBus
        & $scriptPath @boundary | Out-Null
        $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
        $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
        if (-not $command.Contains("--single $boundaryBus '$validRender' '$validCapture' 15")) {
            throw "Single-bus boundary index $boundaryBus generated an unexpected command: $command"
        }
    }

    $singleWithExtraPair = $base.Clone()
    $singleWithExtraPair.Mode = 'single_bus'
    $singleWithExtraPair.RenderId2 = $validRender2
    $singleWithExtraPair.CaptureId2 = $validCapture2
    Assert-Rejected $singleWithExtraPair 'single_bus mode accepts exactly one endpoint pair.'

    $singleWithOutOfRangeBus = $base.Clone()
    $singleWithOutOfRangeBus.Mode = 'single_bus'
    $singleWithOutOfRangeBus.BusIndex = 8
    try {
        & $scriptPath @singleWithOutOfRangeBus | Out-Null
        throw 'Expected BusIndex validation failure.'
    } catch {
        if ($_.Exception.Message -notmatch 'BusIndex') { throw }
    }

    Write-Output 'lab_interactive_probe_tests passed=27'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
