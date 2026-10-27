# Starts the game, waits until it is closed, then prints the plugin log lines
# written during the session. Meant to run as a shared-queue job while the user
# plays. Exits 1 when the plugin refused the executable or failed to start.
param(
    [string]$GameDir = 'D:\Program Files\Games\Steam\steamapps\common\Amnesia The Dark Descent',
    [string]$Exe = 'Amnesia.exe'
)

$ErrorActionPreference = 'Stop'
$log = Join-Path $GameDir 'AmnesiaTDD.FPSFix.log'
$exePath = Join-Path $GameDir $Exe

$running = Get-Process -Name 'Amnesia*' -ErrorAction SilentlyContinue
if ($running) {
    Write-Output "An Amnesia process is already running (PID $($running.Id -join ', ')); not starting another."
    exit 1
}

$logStart = 0
if (Test-Path $log) {
    $logStart = (Get-Item $log).Length
}

Write-Output "Starting $exePath"
$game = Start-Process -FilePath $exePath -WorkingDirectory $GameDir -PassThru
$game.WaitForExit()
Write-Output "Game exited with code $($game.ExitCode)"

if (-not (Test-Path $log)) {
    Write-Output "No plugin log at $log; the ASI was not loaded."
    exit 1
}

# Read only what this session appended.
$stream = [System.IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
try {
    [void]$stream.Seek($logStart, 'Begin')
    $session = (New-Object System.IO.StreamReader($stream)).ReadToEnd()
} finally {
    $stream.Dispose()
}

Write-Output '--- plugin log for this session ---'
Write-Output $session.TrimEnd()

if ($session -notmatch 'Compatible x86 executable signatures accepted' -or
    $session -match 'Fail-closed|Hook initialization failed') {
    Write-Output '--- the plugin did not run in this session ---'
    exit 1
}
exit 0
