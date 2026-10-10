param(
    [Parameter(Mandatory = $true)]
    [string] $PackagePath
)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$required = @(
    'ComponentizedAudioSample.inf',
    'TabletAudioSample.sys',
    'KeywordDetectorContosoAdapter.dll',
    'sysvad.cat'
)

foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $name) -PathType Leaf)) {
        throw "Missing driver package file: $name"
    }
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$secureBoot = try { Confirm-SecureBootUEFI } catch { $null }
$bcd = & bcdedit /enum '{current}' 2>&1
$testSigning = [bool]($bcd | Select-String -Pattern 'testsigning\s+Yes' -Quiet)
$catalog = Get-AuthenticodeSignature -LiteralPath (Join-Path $package 'sysvad.cat')
$files = foreach ($name in $required) {
    $file = Join-Path $package $name
    [pscustomobject]@{
        Name = $name
        Sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    }
}

[pscustomobject]@{
    Computer = $env:COMPUTERNAME
    Identity = $identity.Name
    IsAdministrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    SecureBoot = $secureBoot
    TestSigning = $testSigning
    CatalogSignature = [string]$catalog.Status
    PackagePath = $package
    Files = $files
} | ConvertTo-Json -Depth 4
