$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'lab-interactive-probe.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "sar-interactive-probe-$([guid]::NewGuid().ToString('N'))"
$probePath = Join-Path $root 'probe.exe'
$reportPath = Join-Path $root 'report.txt'
$validRender = '{0.0.0.00000000}.{11111111-1111-1111-1111-111111111111}'
$validCapture = '{0.0.1.00000000}.{22222222-2222-2222-2222-222222222222}'
$validRender2 = '{0.0.0.00000000}.{33333333-3333-3333-3333-333333333333}'
$validCapture2 = '{0.0.1.00000000}.{44444444-4444-4444-4444-444444444444}'

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

    Write-Output 'lab_interactive_probe_tests passed=3'
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
