#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Enable/disable Lenovo EinkSvr automatic start (for RE isolation tests).

.DESCRIPTION
  Homebar UI is drawn by Lenovo processes started via EinkSvr. Disabling
  autostart lets you reboot without that stack, then try USB replay / Linux
  instead. Does not uninstall anything.

.PARAMETER Mode
  Disabled  - never start at boot (use for isolation test)
  Manual    - start only when you Start-Service / Scenario S
  Automatic - Lenovo default

.PARAMETER StopNow
  Also stop EinkSvr and related EInk* processes immediately.

.PARAMETER StartNow
  Start EinkSvr after changing the mode (useful when restoring Automatic).

.EXAMPLE
  .\Set-EinkSvrAutostart.ps1 -Mode Disabled -StopNow
.EXAMPLE
  .\Set-EinkSvrAutostart.ps1 -Mode Automatic -StartNow
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('Disabled', 'Manual', 'Automatic')]
	[string]$Mode,

	[switch]$StopNow,

	[switch]$StartNow
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$svc = Get-Service -Name 'EinkSvr' -ErrorAction Stop
Write-Host ("EinkSvr was: Status={0} StartType={1}" -f $svc.Status, $svc.StartType)

Set-Service -Name 'EinkSvr' -StartupType $Mode
$svc.Refresh()
Write-Host ("EinkSvr now: StartType={0}" -f $svc.StartType) -ForegroundColor Green

$names = @(
	'EInkHomebar', 'EInkDrawBoard', 'EInkMuti', 'EInkSettings',
	'EInkSmartInfo', 'EInkSvr'
)

if ($StopNow) {
	if ($svc.Status -ne 'Stopped') {
		Stop-Service -Name 'EinkSvr' -Force -ErrorAction SilentlyContinue
		Start-Sleep -Seconds 2
	}
	Get-Process -Name $names -ErrorAction SilentlyContinue |
		Stop-Process -Force -ErrorAction SilentlyContinue
	Write-Host 'Stopped EinkSvr + EInk* processes.'
}

if ($StartNow) {
	Start-Service -Name 'EinkSvr'
	Write-Host 'Started EinkSvr.'
}

Write-Host ''
Write-Host 'Notes:'
Write-Host '  - Homebar UI will not appear unless EinkSvr (or your own app) draws it.'
Write-Host '  - Firmware HID keyboard may still work without EinkSvr.'
Write-Host '  - Restore later:  .\Set-EinkSvrAutostart.ps1 -Mode Automatic -StartNow'
Write-Host '  - Scenario S still works if StartType is Manual (Start-Service is allowed).'
