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
    function Start-ScheduledTask { Set-Content -LiteralPath $reportPath -Value 'PROBE_EXIT=0' }
    function Get-ScheduledTask { [pscustomobject]@{ State = 'Ready' } }
    function Unregister-ScheduledTask {}
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

    $singleWithExtra = $base.Clone()
    $singleWithExtra.RenderId2 = $validRender2
    $singleWithExtra.CaptureId2 = $validCapture2
    Assert-Rejected $singleWithExtra 'RenderId2 and CaptureId2 are only valid in dual_bus mode.'

    $invalidEndpoint = $base.Clone()
    $invalidEndpoint.Mode = 'dual_bus'
    $invalidEndpoint.RenderId2 = 'not-a-wasapi-id'
    $invalidEndpoint.CaptureId2 = $validCapture2
    Assert-Rejected $invalidEndpoint 'Invalid WASAPI endpoint ID:'

    $oneExtra = $base.Clone()
    $oneExtra.Mode = 'multi_bus'
    Assert-Rejected $oneExtra 'Multi-bus mode requires two to four endpoint pairs.'

    $mismatched = $base.Clone()
    $mismatched.Mode = 'multi_bus'
    $mismatched.AdditionalRenderIds = @($validRender2, $validRender3)
    $mismatched.AdditionalCaptureIds = @($validCapture2)
    Assert-Rejected $mismatched 'Multi-bus mode requires two to four endpoint pairs.'

    $tooMany = $base.Clone()
    $tooMany.Mode = 'multi_bus'
    $tooMany.AdditionalRenderIds = @($validRender2, $validRender3, $validRender4, $validRender)
    $tooMany.AdditionalCaptureIds = @($validCapture2, $validCapture3, $validCapture4, $validCapture)
    Assert-Rejected $tooMany 'Multi-bus mode requires two to four endpoint pairs.'

    $badFourthEndpoint = $base.Clone()
    $badFourthEndpoint.Mode = 'multi_bus'
    $badFourthEndpoint.AdditionalRenderIds = @($validRender2, $validRender3, 'bad-endpoint')
    $badFourthEndpoint.AdditionalCaptureIds = @($validCapture2, $validCapture3, $validCapture4)
    Assert-Rejected $badFourthEndpoint 'Invalid WASAPI endpoint ID:'

    $legacyPlusMulti = $base.Clone()
    $legacyPlusMulti.Mode = 'multi_bus'
    $legacyPlusMulti.RenderId2 = $validRender2
    $legacyPlusMulti.CaptureId2 = $validCapture2
    $legacyPlusMulti.AdditionalRenderIds = @($validRender3)
    $legacyPlusMulti.AdditionalCaptureIds = @($validCapture3)
    Assert-Rejected $legacyPlusMulti 'RenderId2 and CaptureId2 cannot be combined with multi_bus mode.'

    $multi = $base.Clone()
    $multi.Mode = 'multi_bus'
    $multi.AdditionalRenderIds = @($validRender2, $validRender3, $validRender4)
    $multi.AdditionalCaptureIds = @($validCapture2, $validCapture3, $validCapture4)
    & $scriptPath @multi | Out-Null
    $encoded = [regex]::Match($global:SarCapturedActionArgument, '-EncodedCommand ([A-Za-z0-9+/=]+)').Groups[1].Value
    $command = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    $expectedMultiArguments = "--multi 15 '$validRender' '$validCapture' '$validRender2' '$validCapture2' '$validRender3' '$validCapture3' '$validRender4' '$validCapture4'"
    if (-not $command.Contains($expectedMultiArguments)) {
        throw "Four-bus launcher generated an unexpected command: $command"
    }

    Write-Output 'lab_interactive_probe_tests passed=9'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
