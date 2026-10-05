[CmdletBinding()]
param(
    [string] $ProbePath = '',
    [string] $InventoryPath = '',
    [string] $ReportPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([bool]$ProbePath -eq [bool]$InventoryPath) {
    throw 'Specify exactly one of ProbePath or InventoryPath.'
}

if ($ProbePath) {
    if ([Diagnostics.Process]::GetCurrentProcess().SessionId -eq 0) {
        throw 'Enumerate from a logged-on desktop session, not WinRM Session 0.'
    }
    $probe = (Resolve-Path -LiteralPath $ProbePath).Path
    $lines = @(& $probe --list 2>&1 | ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) { throw "Endpoint probe exited with code $LASTEXITCODE." }
} else {
    $lines = @(Get-Content -LiteralPath $InventoryPath)
}

$endpoints = @()
$direction = ''
for ($i = 0; $i -lt $lines.Count; ++$i) {
    if ($lines[$i] -eq 'Render endpoints:') { $direction = 'render'; continue }
    if ($lines[$i] -eq 'Capture endpoints:') { $direction = 'capture'; continue }
    if ($direction -and $lines[$i] -match '^  (\S.*)$') {
        $name = $Matches[1]
        if ($i + 1 -ge $lines.Count -or
            $lines[$i + 1] -notmatch '^    (\{0\.0\.[01]\.00000000\}\.\{[0-9a-fA-F-]{36}\})$') {
            throw "Missing endpoint ID after '$name'."
        }
        $endpoints += [pscustomobject]@{
            Direction = $direction
            Name = $name
            Id = $Matches[1]
        }
        ++$i
    }
}

$sample = @($endpoints | Where-Object {
    $_.Name -like '*Virtual Audio Device (WDM) - Tablet Sample*'
})
$speaker = @($sample | Where-Object {
    $_.Direction -eq 'render' -and
    $_.Name -eq 'Speakers (Virtual Audio Device (WDM) - Tablet Sample)'
})
$microphone = @($sample | Where-Object {
    $_.Direction -eq 'capture' -and
    $_.Name -eq 'Internal Microphone Array - Front (Virtual Audio Device (WDM) - Tablet Sample)'
})
$passed = $sample.Count -eq 2 -and $speaker.Count -eq 1 -and
          $microphone.Count -eq 1 -and $speaker[0].Id -ne $microphone[0].Id
$report = [pscustomobject]@{
    Passed = $passed
    ActiveEndpointCount = $endpoints.Count
    SampleEndpointCount = $sample.Count
    SampleEndpoints = $sample
}
$json = $report | ConvertTo-Json -Depth 4
if ($ReportPath) { $json | Set-Content -LiteralPath $ReportPath -Encoding UTF8 }
Write-Output $json
if (!$passed) { exit 1 }
