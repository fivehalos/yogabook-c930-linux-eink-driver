# Invoked by the YogaBookEinkUsbColdbootCapture scheduled task.
# Do not use #Requires here - Task Scheduler already runs this elevated.
$ErrorActionPreference = 'Continue'
$TaskName = 'YogaBookEinkUsbColdbootCapture'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent (Split-Path -Parent $here)
$logDir = Join-Path $repo 'win-captures'
$log = Join-Path $logDir 'coldboot-task.log'
$capture = Join-Path $here 'Capture-EinkUsb.ps1'

New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Write-Log([string]$msg) {
	$line = '{0:yyyy-MM-dd HH:mm:ss}  {1}' -f (Get-Date), $msg
	Add-Content -LiteralPath $log -Value $line -Encoding UTF8
}

Write-Log 'Run-ColdbootTask starting'
Write-Log ("capture script: {0} exists={1}" -f $capture, (Test-Path -LiteralPath $capture))

$idle = 90
if ($env:EINK_COLDBOOT_IDLE) {
	$idle = [int]$env:EINK_COLDBOOT_IDLE
}

try {
	$p = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
		'-NoProfile',
		'-ExecutionPolicy', 'Bypass',
		'-File', $capture,
		'-Scenario', 'A',
		'-IdleSeconds', "$idle",
		'-Force'
	) -Wait -PassThru -WindowStyle Hidden
	Write-Log ("Capture-EinkUsb exit={0}" -f $p.ExitCode)
} catch {
	Write-Log ("ERROR: {0}" -f $_.Exception.Message)
} finally {
	try {
		Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
		Write-Log 'Unregistered scheduled task'
	} catch {
		Write-Log ("Unregister failed: {0}" -f $_.Exception.Message)
	}
	Write-Log 'Run-ColdbootTask finished'
}
