$ErrorActionPreference = 'Stop'
$checker = Join-Path $PSScriptRoot 'lab-endpoint-inventory.ps1'
$shell = (Get-Process -Id $PID).Path
$inventory = Join-Path ([IO.Path]::GetTempPath()) "sar-endpoints-$([guid]::NewGuid().ToString('N')).txt"
$pair = @'
Render endpoints:
  Speakers (System Audio Route Experimental)
    {0.0.0.00000000}.{11111111-1111-1111-1111-111111111111}
  Headphones (System Audio Route Experimental)
    {0.0.0.00000000}.{55555555-5555-5555-5555-555555555555}
  CABLE Input (VB-Audio Virtual Cable)
    {0.0.0.00000000}.{22222222-2222-2222-2222-222222222222}
Capture endpoints:
  SAR Experimental Capture 1 (System Audio Route Experimental)
    {0.0.1.00000000}.{33333333-3333-3333-3333-333333333333}
  SAR Experimental Capture 2 (System Audio Route Experimental)
    {0.0.1.00000000}.{66666666-6666-6666-6666-666666666666}
'@
$extra = @'
  SAR Experimental Capture 3 (System Audio Route Experimental)
    {0.0.1.00000000}.{44444444-4444-4444-4444-444444444444}
'@

try {
    foreach ($case in @(
        @{ Name = 'pairs'; Text = $pair; Passed = $true; Count = 4 },
        @{ Name = 'extra'; Text = "$pair`n$extra"; Passed = $false; Count = 5 },
        @{ Name = 'missing'; Text = $pair -replace 'SAR Experimental Capture 1', 'Other Microphone'; Passed = $false; Count = 4 },
        @{ Name = 'duplicate-id'; Text = $pair -replace '66666666-6666-6666-6666-666666666666', '33333333-3333-3333-3333-333333333333'; Passed = $false; Count = 4 }
    )) {
        Set-Content -LiteralPath $inventory -Value $case.Text -Encoding UTF8
        $output = & $shell -NoProfile -File $checker -InventoryPath $inventory
        $exit = $LASTEXITCODE
        $report = ($output -join "`n") | ConvertFrom-Json
        if ($report.Passed -ne $case.Passed -or
            $report.SampleEndpointCount -ne $case.Count -or
            $exit -ne [int](!$case.Passed)) {
            throw "Inventory case '$($case.Name)' failed: exit=$exit report=$($output -join ' ')"
        }
    }
    Write-Output 'lab_endpoint_inventory_tests passed=4'
} finally {
    Remove-Item -LiteralPath $inventory -ErrorAction SilentlyContinue
}
exit 0
