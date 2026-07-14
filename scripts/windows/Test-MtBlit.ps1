<#
.SYNOPSIS
  Guided test: multitouch Homebar mode + owned A8/A94 blits.

.DESCRIPTION
  EinkSvr owns MI_00 while Homebar runs, so we cannot blit until it is
  stopped. Sequence:

    1. Enter multitouch mode via Homebar (artifacts OK).
    2. Stop EinkSvr (releases bulk).
    3. scenario-get + fill white / black / stripes via EinkWinUsb.
    4. You note what cleared vs what ghosted; whether 2/3 finger still works.

  See docs/WINDOWS_MULTITOUCH_BRIEF.md (display ownership question).
#>
[CmdletBinding()]
param(
	[switch]$SkipHomebarPrompt,
	[switch]$KeepEinkSvrStopped
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path (Join-Path $repo 'scripts\windows\eink-winusb\EinkWinUsb.cs'))) {
	$repo = Split-Path -Parent $PSScriptRoot
}
Set-Location $repo

$tool = Join-Path $repo 'scripts\windows\eink-winusb\EinkWinUsb.exe'
$build = Join-Path $repo 'scripts\windows\eink-winusb\build.ps1'
$einkScript = Join-Path $repo 'scripts\windows\Set-EinkSvrAutostart.ps1'
$notes = Join-Path $repo 'win-captures\capture-notes.txt'

function Pause-Step([string]$msg) {
	Write-Host ''
	Write-Host $msg -ForegroundColor Cyan
	Read-Host 'Press Enter to continue'
}

function Invoke-ElevatedEinkSvr([string]$Mode, [switch]$StartNow, [switch]$StopNow) {
	$args = @('-ExecutionPolicy', 'Bypass', '-File', $einkScript, '-Mode', $Mode)
	if ($StartNow) { $args += '-StartNow' }
	if ($StopNow) { $args += '-StopNow' }
	$p = Start-Process -FilePath powershell.exe -ArgumentList $args -Verb RunAs -PassThru -Wait
	if ($p.ExitCode -ne 0) {
		throw "Set-EinkSvrAutostart exited $($p.ExitCode)"
	}
}

Write-Host '=== MT + blit experiment ===' -ForegroundColor Green
Write-Host 'Goal: see what full-frame owner blits clear after multitouch Homebar mode.'

if (-not $SkipHomebarPrompt) {
	Pause-Step @"
STEP 1 - Multitouch mode (EinkSvr must be Running / Homebar visible)
  - Homebar -> pen / touchpad / mouse (the mode that had multitouch)
  - Put some artifacts: homebar chrome, cursors, finger ink if any
  - Confirm 2-finger still works; Notepad on LCD stays clean
  - Leave that picture on the E-Ink - do not clear it
"@
}

Write-Host 'Building EinkWinUsb...'
& $build

Pause-Step @"
STEP 2 - Stop EinkSvr (required to open WinUSB MI_00)
  After Enter, service stops. Note on E-Ink:
    STAYS = leftover pixels/ghosts after Homebar exits
    GOES  = UI that disappears when EinkSvr dies
  Also try 2/3 finger briefly - did MT survive service stop?
"@

Invoke-ElevatedEinkSvr -Mode Manual -StopNow
Start-Sleep -Seconds 2
Get-Service EinkSvr | Format-List Name, Status, StartType

Write-Host 'scenario-get after stop:'
& $tool scenario-get

Pause-Step @"
STEP 3 - fill WHITE (0xff) full panel
  Watch E-Ink. After Enter we blit white.
  Note what CLEARS vs what still ghosts.
"@
& $tool fill white

Pause-Step @"
STEP 4 - fill BLACK (0x00)
  Note residual ghost from white / old Homebar.
"@
& $tool fill black

Pause-Step @"
STEP 5 - STRIPES (64px black/white bands)
  Clean bands = blit path healthy. Muddy/smear = waveform or buffer mismatch.
"@
& $tool stripes

Write-Host ''
Write-Host 'Optional: remount MT without Homebar (E-capture A6 path):' -ForegroundColor DarkGray
Write-Host "  $tool mt-replay" -ForegroundColor DarkGray
Write-Host 'Then check whether 2/3 finger returns on E-Ink.' -ForegroundColor DarkGray

$summary = Read-Host 'One-line result for capture-notes (or blank to skip)'
if ($summary) {
	$line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  MT-blit: $summary"
	Add-Content -LiteralPath $notes -Value $line -Encoding utf8
	Write-Host "Appended to $notes"
}

if (-not $KeepEinkSvrStopped) {
	$r = Read-Host 'Restart EinkSvr Automatic now? [Y/n]'
	if ($r -notmatch '^[Nn]') {
		Invoke-ElevatedEinkSvr -Mode Automatic -StartNow
		Get-Service EinkSvr | Format-List Name, Status, StartType
	}
}

Write-Host 'Done.' -ForegroundColor Green
