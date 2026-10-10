param(
    [Parameter(Mandatory)] [string] $ProbePath,
    [Parameter(Mandatory)] [string] $RenderId,
    [Parameter(Mandatory)] [string] $CaptureId,
    [ValidateSet('run', 'default', 'exclusive', 'route', 'single_bus', 'dual_bus', 'multi_bus')] [string] $Mode = 'run',
    [ValidateRange(0, 3)] [int] $BusIndex = 0,
    [string] $RenderId2,
    [string] $CaptureId2,
    [string[]] $AdditionalRenderIds = @(),
    [string[]] $AdditionalCaptureIds = @(),
    [ValidateRange(5, 3600)] [int] $DurationSeconds = 15,
    [Parameter(Mandatory)] [string] $OutputPath
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $ProbePath -PathType Leaf)) {
    throw "Probe not found: $ProbePath"
}
$ids = @($RenderId, $CaptureId)
if ($Mode -eq 'single_bus') {
    if (-not [string]::IsNullOrWhiteSpace($RenderId2) -or
        -not [string]::IsNullOrWhiteSpace($CaptureId2) -or
        $AdditionalRenderIds.Count -ne 0 -or $AdditionalCaptureIds.Count -ne 0) {
        throw 'single_bus mode accepts exactly one endpoint pair.'
    }
} elseif ($Mode -eq 'dual_bus') {
    if ([string]::IsNullOrWhiteSpace($RenderId2) -or
        [string]::IsNullOrWhiteSpace($CaptureId2)) {
        throw 'dual_bus mode requires RenderId2 and CaptureId2.'
    }
    $ids += @($RenderId2, $CaptureId2)
    if ($AdditionalRenderIds.Count -ne 0 -or $AdditionalCaptureIds.Count -ne 0) {
        throw 'Additional endpoint IDs are only valid in multi_bus mode.'
    }
} elseif ($Mode -eq 'multi_bus') {
    if (-not [string]::IsNullOrWhiteSpace($RenderId2) -or
        -not [string]::IsNullOrWhiteSpace($CaptureId2)) {
        throw 'RenderId2 and CaptureId2 cannot be combined with multi_bus mode.'
    }
    if ($AdditionalRenderIds.Count -ne $AdditionalCaptureIds.Count -or
        $AdditionalRenderIds.Count -lt 1 -or $AdditionalRenderIds.Count -gt 3) {
        throw 'Multi-bus mode requires two to four endpoint pairs.'
    }
    for ($index = 0; $index -lt $AdditionalRenderIds.Count; $index++) {
        $ids += @($AdditionalRenderIds[$index], $AdditionalCaptureIds[$index])
    }
} elseif (-not [string]::IsNullOrWhiteSpace($RenderId2) -or
          -not [string]::IsNullOrWhiteSpace($CaptureId2)) {
    throw 'RenderId2 and CaptureId2 are only valid in dual_bus mode.'
} elseif ($AdditionalRenderIds.Count -ne 0 -or $AdditionalCaptureIds.Count -ne 0) {
    throw 'Additional endpoint IDs are only valid in multi_bus mode.'
}
foreach ($id in $ids) {
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
if ($Mode -eq 'single_bus') {
    $command = "& '$probeLiteral' --single $BusIndex '$RenderId' '$CaptureId' $DurationSeconds *> '$outputLiteral'; "
} elseif ($Mode -eq 'dual_bus') {
    $command = "& '$probeLiteral' '$RenderId' '$CaptureId' '$RenderId2' '$CaptureId2' $DurationSeconds *> '$outputLiteral'; "
} elseif ($Mode -eq 'multi_bus') {
    $quotedIds = @($ids | ForEach-Object { "'$_'" }) -join ' '
    $command = "& '$probeLiteral' --multi $DurationSeconds $quotedIds *> '$outputLiteral'; "
} else {
    $command = "& '$probeLiteral' --$Mode '$RenderId' '$CaptureId' *> '$outputLiteral'; "
}
$command +=
           "'PROBE_EXIT=' + `$LASTEXITCODE | Add-Content -LiteralPath '$outputLiteral'"
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -EncodedCommand $encoded"
$principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\$env:USERNAME" -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds ($DurationSeconds + 60))

try {
    Remove-Item -LiteralPath $OutputPath -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings -Force | Out-Null
    Start-ScheduledTask -TaskName $taskName
    $deadline = (Get-Date).AddSeconds($DurationSeconds + 30)
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
