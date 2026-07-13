#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Arm / run / disarm idle A-coldboot USB capture for YogaBook C930 E-Ink.

.DESCRIPTION
  Captures after login with no Homebar interaction - EinkSvr / Homebar enable
  traffic. Uses Capture-EinkUsb.ps1 -Scenario A via Run-ColdbootTask.ps1.

.PARAMETER Arm
  Register a one-shot elevated Scheduled Task at next logon, then exit.
  Reboot (or log off/on) to record A-coldboot.pcap.

.PARAMETER CaptureNow
  Run the idle capture immediately (no reboot). Useful smoke test; misses
  true boot-time EinkSvr bring-up.

.PARAMETER Disarm
  Remove the scheduled task if present.

.PARAMETER IdleSeconds
  Idle capture length for -CaptureNow (default 90). Armed logon capture uses
  Run-ColdbootTask.ps1 (90s, override with env EINK_COLDBOOT_IDLE).

.PARAMETER OutDir
  Passed through to Capture-EinkUsb.ps1 for -CaptureNow only.

.PARAMETER Force
  Overwrite existing A-coldboot.pcap (-CaptureNow).

.EXAMPLE
  .\Capture-EinkColdboot.ps1 -Arm
  # reboot, log in, do not touch E-Ink for ~100s

.EXAMPLE
  .\Capture-EinkColdboot.ps1 -CaptureNow -Force
#>
[CmdletBinding(DefaultParameterSetName = 'CaptureNow')]
param(
	[Parameter(ParameterSetName = 'Arm')]
	[switch]$Arm,

	[Parameter(ParameterSetName = 'CaptureNow')]
	[switch]$CaptureNow,

	[Parameter(ParameterSetName = 'Disarm')]
	[switch]$Disarm,

	[int]$IdleSeconds = 90,

	[string]$OutDir = '',

	[switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$TaskName = 'YogaBookEinkUsbColdbootCapture'
$CaptureScript = Join-Path $PSScriptRoot 'Capture-EinkUsb.ps1'
$RunnerScript = Join-Path $PSScriptRoot 'Run-ColdbootTask.ps1'

function Test-IsAdministrator {
	$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
	$principal = New-Object Security.Principal.WindowsPrincipal($identity)
	return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Remove-ColdbootTask {
	$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
	if ($existing) {
		Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
		Write-Host "Removed scheduled task: $TaskName"
	} else {
		Write-Host "No scheduled task named $TaskName"
	}
}

function Invoke-ColdbootCapture {
	if (-not (Test-Path -LiteralPath $CaptureScript)) {
		throw "Missing companion script: $CaptureScript"
	}
	$argList = @(
		'-NoProfile',
		'-ExecutionPolicy', 'Bypass',
		'-File', $CaptureScript,
		'-Scenario', 'A',
		'-IdleSeconds', "$IdleSeconds"
	)
	if ($OutDir) {
		$argList += @('-OutDir', $OutDir)
	}
	if ($Force) {
		$argList += '-Force'
	}

	Write-Host 'Running idle A-coldboot capture...'
	& powershell.exe @argList
	if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
		throw "Capture-EinkUsb.ps1 failed with exit code $LASTEXITCODE"
	}
}

if (-not (Test-IsAdministrator)) {
	throw 'Run elevated (Administrator).'
}

if (-not $Arm -and -not $Disarm -and -not $CaptureNow) {
	$CaptureNow = $true
}

if ($Disarm) {
	Remove-ColdbootTask
	return
}

if ($CaptureNow) {
	Invoke-ColdbootCapture
	return
}

if ($Arm) {
	if (-not (Test-Path -LiteralPath $RunnerScript)) {
		throw "Missing companion script: $RunnerScript"
	}

	Remove-ColdbootTask

	# Short action line - long inline powershell args were truncated by Task Scheduler.
	$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
		-Argument "-NoProfile -ExecutionPolicy Bypass -File `"$RunnerScript`""
	$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
	# Short delay: capture must include EinkSvr start, not start after it.
	$trigger.Delay = 'PT10S'
	$principal = New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest
	$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
		-StartWhenAvailable -MultipleInstances IgnoreNew

	Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
		-Principal $principal -Settings $settings -Force | Out-Null

	$check = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
	Write-Host ''
	Write-Host "Armed one-shot task: $TaskName (state=$($check.State))" -ForegroundColor Green
	Write-Host 'Next steps:'
	Write-Host '  1. Reboot (preferred) or sign out/in.'
	Write-Host '  2. Log in and leave the E-Ink untouched for ~100s (10s delay + 90s capture).'
	Write-Host '  3. Find A-coldboot.pcap under win-captures\'
	Write-Host '  4. If missing, read win-captures\coldboot-task.log'
	Write-Host ''
	Write-Host 'Manual alternative after reboot (more reliable):'
	Write-Host '  Immediately after login, elevated:'
	Write-Host '  powershell -ExecutionPolicy Bypass -File .\scripts\windows\Capture-EinkColdboot.ps1 -CaptureNow -Force'
	return
}
