param(
    [Parameter(Mandatory)] [string] $ProbePath,
    [Parameter(Mandatory)] [string] $RenderId,
    [Parameter(Mandatory)] [string] $CaptureId,
    [ValidateSet('run', 'default', 'exclusive')] [string] $Mode = 'run',
    [Parameter(Mandatory)] [string] $OutputPath
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $ProbePath -PathType Leaf)) {
    throw "Probe not found: $ProbePath"
}
foreach ($id in @($RenderId, $CaptureId)) {
    if ($id -notmatch '^\{0\.0\.[01]\.00000000\}\.\{[0-9a-fA-F-]{36}\}$') {
        throw "Invalid WASAPI endpoint ID: $id"
    }
}
if (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue |
          Where-Object SessionId -gt 0)) {
    throw 'No interactive desktop session is logged on.'
}

$taskName = 'SARLab-InteractiveAudioProbe'
$probeLiteral = $ProbePath.Replace("'", "''")
$outputLiteral = $OutputPath.Replace("'", "''")
$command = "& '$probeLiteral' --$Mode '$RenderId' '$CaptureId' *> '$outputLiteral'; " +
           "'PROBE_EXIT=' + `$LASTEXITCODE | Add-Content -LiteralPath '$outputLiteral'"
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -EncodedCommand $encoded"
$principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\$env:USERNAME" -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2)

try {
    Remove-Item -LiteralPath $OutputPath -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings -Force | Out-Null
    Start-ScheduledTask -TaskName $taskName
    $deadline = (Get-Date).AddMinutes(2)
    do {
        Start-Sleep -Milliseconds 500
        $task = Get-ScheduledTask -TaskName $taskName
    } while (($task.State -eq 'Running' -or -not (Test-Path -LiteralPath $OutputPath)) -and
             (Get-Date) -lt $deadline)
    if ((Get-Date) -ge $deadline) { throw 'Interactive probe timed out.' }
    if (-not (Test-Path -LiteralPath $OutputPath)) {
        throw "Interactive task completed without a probe report: $((Get-ScheduledTaskInfo -TaskName $taskName).LastTaskResult)"
    }
    Get-Content -LiteralPath $OutputPath
} finally {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
}
