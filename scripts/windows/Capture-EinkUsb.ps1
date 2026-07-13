#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Guided USB capture for YogaBook C930 E-Ink (048d:8951) via USBPcapCMD.

.DESCRIPTION
  Automates start/stop timing from docs/WINDOWS_CAPTURE.md. Homebar taps stay
  manual - the script prompts for each action.

.PARAMETER Scenario
  A = coldboot idle (no prompts; use -IdleSeconds),
  B = keyboard, C = pen-mouse (default), D = roundtrip,
  S = stop EinkSvr, capture, start EinkSvr (enable/init traffic),
  All = B then C then D.
  Prefer Capture-EinkColdboot.ps1 to arm A across a reboot.

.PARAMETER OutDir
  Output directory for .pcap files and capture-notes.txt.

.PARAMETER Force
  Overwrite existing capture files.

.PARAMETER UsbPcapCmd
  Path to USBPcapCMD.exe.

.PARAMETER Interface
  Optional USBPcap control device, e.g. \\.\USBPcap2. Auto-detected when empty.

.PARAMETER SettleBeforeSec
  Idle seconds after capture starts before prompting for action.

.PARAMETER SettleAfterSec
  Idle seconds after the user finishes before stopping capture.

.PARAMETER IdleSeconds
  For Scenario A: capture duration with no Homebar prompts (default 30).

.EXAMPLE
  .\Capture-EinkUsb.ps1 -Scenario C
.EXAMPLE
  .\Capture-EinkUsb.ps1 -Scenario All -Force
.EXAMPLE
  .\Capture-EinkUsb.ps1 -Scenario A -IdleSeconds 30 -Force
.EXAMPLE
  .\Capture-EinkUsb.ps1 -Scenario S -Force
#>
[CmdletBinding()]
param(
	[ValidateSet('A', 'B', 'C', 'D', 'S', 'All')]
	[string]$Scenario = 'C',

	[string]$OutDir = '',

	[switch]$Force,

	[string]$UsbPcapCmd = 'C:\Program Files\USBPcap\USBPcapCMD.exe',

	[string]$Interface = '',

	[int]$SettleBeforeSec = 5,

	[int]$SettleAfterSec = 10,

	[int]$IdleSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$VidPidNeedle = '048D.*8951|8951.*048D|VID_048D&PID_8951|048d:8951'
$DefaultRepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $OutDir) {
	$OutDir = Join-Path $DefaultRepoRoot 'win-captures'
}

$ScenarioMeta = @{
	'A' = @{
		File   = 'A-coldboot.pcap'
		Title  = 'coldboot-idle'
		Prompt = 'Do not touch the E-Ink panel.'
		Idle   = $true
		ServiceRestart = $false
	}
	'B' = @{
		File   = 'B-keyboard.pcap'
		Title  = 'keyboard'
		Idle   = $false
		ServiceRestart = $false
		Prompt = @'
ACTION (B-keyboard):
  1. Open Notepad on the main LCD.
  2. Homebar -> keyboard.
  3. Type on the E-Ink panel; chars should appear in Notepad.
  Press Enter here when finished.
'@
	}
	'C' = @{
		File   = 'C-penmouse.pcap'
		Title  = 'pen-mouse'
		Idle   = $false
		ServiceRestart = $false
		Prompt = @'
ACTION (C-penmouse) - PRIORITY:
  1. Open Notepad on the main LCD.
  2. Homebar -> pen / touchpad.
  3. Drag on the E-Ink panel; Notepad must NOT receive typing.
  Press Enter here when finished.
'@
	}
	'D' = @{
		File   = 'D-roundtrip.pcap'
		Title  = 'roundtrip'
		Idle   = $false
		ServiceRestart = $false
		Prompt = @'
ACTION (D-roundtrip):
  Homebar: keyboard -> pen-mouse -> keyboard -> pen-mouse.
  Press Enter here when finished.
'@
	}
	'S' = @{
		File   = 'S-einksvr-restart.pcap'
		Title  = 'EinkSvr-restart'
		Idle   = $false
		ServiceRestart = $true
		Prompt = 'Automated: stop EinkSvr stack, capture, start EinkSvr again. Do not touch the E-Ink.'
	}
}

function Test-IsAdministrator {
	$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
	$principal = New-Object Security.Principal.WindowsPrincipal($identity)
	return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Note {
	param([string]$Message)
	$notes = Join-Path $OutDir 'capture-notes.txt'
	$line = '{0:yyyy-MM-dd HH:mm:ss}  {1}' -f (Get-Date), $Message
	Add-Content -LiteralPath $notes -Value $line -Encoding UTF8
	Write-Host $line
}

function Invoke-UsbPcapExtcap {
	param([string[]]$ArgumentList)
	$psi = New-Object System.Diagnostics.ProcessStartInfo
	$psi.FileName = $UsbPcapCmd
	$psi.Arguments = ($ArgumentList | ForEach-Object {
			if ($_ -match '\s') { '"{0}"' -f ($_ -replace '"', '\"') } else { $_ }
		}) -join ' '
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false
	$psi.CreateNoWindow = $true
	$proc = [System.Diagnostics.Process]::Start($psi)
	$stdout = $proc.StandardOutput.ReadToEnd()
	$stderr = $proc.StandardError.ReadToEnd()
	$proc.WaitForExit()
	return [pscustomobject]@{
		ExitCode = $proc.ExitCode
		Stdout   = $stdout
		Stderr   = $stderr
	}
}

function Get-UsbPcapControlDevices {
	# Test-Path \\.\USBPcapN is unreliable (often False when capture works).
	# USBPcapCMD --extcap-interfaces is the source of truth.
	$result = Invoke-UsbPcapExtcap -ArgumentList @('--extcap-interfaces')
	$found = @()
	foreach ($line in ($result.Stdout -split "`r?`n")) {
		if ($line -match 'interface\s+\{value=([^}]+)\}') {
			$found += $Matches[1]
		}
	}
	return @($found | Select-Object -Unique)
}

function Get-UsbPcapInterfaces {
	return @(Get-UsbPcapControlDevices)
}

function Test-EinkDisplayName {
	param([string]$Display)
	return (
		($Display -match $VidPidNeedle) -or
		($Display -match '(?i)VID_048D') -or
		($Display -match '(?i)PID_8951') -or
		($Display -match '(?i)048[dD]:?8951') -or
		($Display -match '(?i)ITE\s*8951') -or
		($Display -match '(?i)E-?Ink')
	)
}

function Find-EinkOnHub {
	param([string]$Interface)

	$result = Invoke-UsbPcapExtcap -ArgumentList @(
		'--extcap-config',
		'--extcap-interface', $Interface
	)
	$text = $result.Stdout + "`n" + $result.Stderr
	$addresses = New-Object System.Collections.Generic.List[string]

	# extcap lines look like:
	# value {arg=99}{value=3}{display=[3] USB Composite Device}{enabled=true}
	foreach ($line in ($text -split "`r?`n")) {
		if ($line -notmatch '(?i)^value\s+\{arg=99\}') { continue }
		if ($line -notmatch '\{enabled=true\}') { continue }
		if ($line -notmatch '\{value=([0-9]+)\}') { continue }
		$addr = $Matches[1]
		$display = ''
		if ($line -match '\{display=([^}]*)\}') {
			$display = $Matches[1]
		}
		if (Test-EinkDisplayName -Display $display) {
			$addresses.Add($addr)
		}
	}

	# If hub dump mentions the panel but parent display text is generic, take
	# enabled composite addresses when VID/PID appears anywhere on the hub.
	if ($addresses.Count -eq 0 -and ($text -match '(?i)048[dD]' -or $text -match '8951')) {
		foreach ($line in ($text -split "`r?`n")) {
			if ($line -notmatch '(?i)^value\s+\{arg=99\}\{value=([0-9]+)\}\{display=([^}]*)\}\{enabled=true\}') {
				continue
			}
			$addr = $Matches[1]
			$display = $Matches[2]
			if ($display -match '(?i)048[dD]|8951|E-?Ink|ITE|Composite') {
				$addresses.Add($addr)
			}
		}
	}

	return [pscustomobject]@{
		Interface = $Interface
		Addresses = @($addresses | Select-Object -Unique)
		Raw       = $text
	}
}

function Resolve-EinkCaptureTarget {
	if ($Interface) {
		$known = @(Get-UsbPcapControlDevices)
		if ($known.Count -gt 0 -and ($known -notcontains $Interface)) {
			throw ("USBPcap interface not listed by extcap: {0} (have: {1})" -f $Interface, ($known -join ', '))
		}
		$found = Find-EinkOnHub -Interface $Interface
		$addrs = $found.Addresses
		return [pscustomobject]@{
			Interface     = $Interface
			DeviceAddress = $(if ($addrs.Count -gt 0) { $addrs -join ',' } else { $null })
			CaptureAll    = ($addrs.Count -eq 0)
		}
	}

	$ifaces = @(Get-UsbPcapInterfaces)
	if ($ifaces.Count -eq 0) {
		throw @'
No USBPcap control devices from --extcap-interfaces.

Fix:
  1. Elevated: & "C:\Program Files\USBPcap\USBPcapCMD.exe" -I
  2. Reboot
  3. Elevated: & "C:\Program Files\USBPcap\USBPcapCMD.exe" --extcap-interfaces
     Expect: interface {value=\\.\USBPcap1}{display=USBPcap1}
'@
	}

	foreach ($iface in $ifaces) {
		$found = Find-EinkOnHub -Interface $iface
		if ($found.Addresses.Count -gt 0) {
			return [pscustomobject]@{
				Interface     = $found.Interface
				DeviceAddress = ($found.Addresses -join ',')
				CaptureAll    = $false
			}
		}
	}

	# Last resort: capture entire first real hub
	$fallback = $ifaces[0]
	Write-Warning ("Could not match 048d:8951 on any hub; using -A on {0}." -f $fallback)
	return [pscustomobject]@{
		Interface     = $fallback
		DeviceAddress = $null
		CaptureAll    = $true
	}
}

function Assert-Preflight {
	if (-not (Test-IsAdministrator)) {
		throw 'Run elevated (Administrator). Right-click PowerShell -> Run as administrator.'
	}
	if (-not (Test-Path -LiteralPath $UsbPcapCmd)) {
		throw "USBPcapCMD not found: $UsbPcapCmd"
	}

	$devices = @(Get-UsbPcapControlDevices)
	if ($devices.Count -eq 0) {
		throw @'
No \\.\USBPcapN devices found - USBPcap filter is not ready for capture.

This C930 uses a USB 3.0 root hub (ROOT_HUB30). After install you must:

  1. Elevated PowerShell:
       & "C:\Program Files\USBPcap\USBPcapCMD.exe" -I
  2. Reboot Windows (required).
  3. Elevated PowerShell again:
       & "C:\Program Files\USBPcap\USBPcapCMD.exe" --extcap-interfaces
     You should see lines like: interface {value=\\.\USBPcap1}...
  4. Wireshark -> Capture -> Options should list USBPcapN.

If still empty: reinstall USBPcapSetup, run -I, reboot once more.
Then re-run this script as Administrator.
'@
	}
	Write-Host ("Preflight: USBPcap devices: {0}" -f ($devices -join ', '))

	$svc = Get-Service -Name 'EinkSvr' -ErrorAction SilentlyContinue
	if (-not $svc) {
		Write-Warning 'EinkSvr service not found - Lenovo E-Ink stack may be missing.'
	} elseif ($svc.Status -ne 'Running') {
		Write-Warning ("EinkSvr is {0} (expected Running)." -f $svc.Status)
	} else {
		Write-Host 'Preflight: EinkSvr Running'
	}

	$hb = Get-Process -Name 'EInkHomebar' -ErrorAction SilentlyContinue
	if (-not $hb) {
		Write-Warning 'EInkHomebar process not running - Homebar may be missing.'
	} else {
		Write-Host 'Preflight: EInkHomebar present'
	}

	New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

function Start-UsbCapture {
	param(
		[string]$OutputFile,
		[pscustomobject]$Target
	)

	$argList = @(
		'--device', $Target.Interface,
		'--output', $OutputFile,
		'--snaplen', '65535',
		'--bufferlen', '1048576',
		'--inject-descriptors'
	)
	if ($Target.CaptureAll -or -not $Target.DeviceAddress) {
		$argList += '--capture-from-all-devices'
	} else {
		$argList += @('--devices', $Target.DeviceAddress)
	}

	Write-Host ("Starting USBPcapCMD: {0} -> {1}" -f $Target.Interface, $OutputFile)
	$proc = Start-Process -FilePath $UsbPcapCmd -ArgumentList $argList `
		-PassThru -WindowStyle Minimized
	Start-Sleep -Seconds 1
	if ($proc.HasExited) {
		throw ("USBPcapCMD exited immediately (code {0}) opening {1}. If you just installed USBPcap, reboot. Also confirm: Test-Path {1}" -f $proc.ExitCode, $Target.Interface)
	}
	return $proc
}

function Stop-UsbCapture {
	param([System.Diagnostics.Process]$Process)
	if (-not $Process) { return }
	if ($Process.HasExited) { return }
	try {
		$Process.CloseMainWindow() | Out-Null
	} catch { }
	Start-Sleep -Milliseconds 500
	if (-not $Process.HasExited) {
		Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
	}
	# Worker children sometimes linger
	Get-Process -Name 'USBPcapCMD' -ErrorAction SilentlyContinue |
		Where-Object { $_.Id -ne $Process.Id } |
		Stop-Process -Force -ErrorAction SilentlyContinue
}

function Stop-EinkStack {
	Write-Host 'Stopping EinkSvr service...'
	$svc = Get-Service -Name 'EinkSvr' -ErrorAction SilentlyContinue
	if ($svc -and $svc.Status -ne 'Stopped') {
		Stop-Service -Name 'EinkSvr' -Force -ErrorAction Stop
		$svc.WaitForStatus('Stopped', '00:00:30')
	}
	$names = @(
		'EInkHomebar', 'EInkDrawBoard', 'EInkMuti', 'EInkSettings',
		'EInkSmartInfo', 'EInkSvr'
	)
	Get-Process -Name $names -ErrorAction SilentlyContinue |
		Stop-Process -Force -ErrorAction SilentlyContinue
	Start-Sleep -Seconds 2
	$left = @(Get-Process -Name $names -ErrorAction SilentlyContinue)
	if ($left.Count -gt 0) {
		Write-Warning ("Still running: {0}" -f (($left | Select-Object -ExpandProperty ProcessName) -join ', '))
	} else {
		Write-Host 'Eink stack stopped.'
	}
}

function Start-EinkStack {
	Write-Host 'Starting EinkSvr service...'
	Start-Service -Name 'EinkSvr' -ErrorAction Stop
	$deadline = (Get-Date).AddSeconds(60)
	do {
		$hb = Get-Process -Name 'EInkHomebar' -ErrorAction SilentlyContinue
		$svc = Get-Service -Name 'EinkSvr'
		if ($svc.Status -eq 'Running' -and $hb) {
			Write-Host 'EinkSvr Running, EInkHomebar present.'
			return
		}
		Start-Sleep -Seconds 1
	} while ((Get-Date) -lt $deadline)
	Write-Warning 'Timed out waiting for EInkHomebar - capture may still include init traffic.'
}

function Invoke-GuidedCapture {
	param(
		[string]$Key,
		[pscustomobject]$Target
	)

	$meta = $ScenarioMeta[$Key]
	$outFile = Join-Path $OutDir $meta.File

	if ((Test-Path -LiteralPath $outFile) -and -not $Force) {
		$existing = Get-Item -LiteralPath $outFile
		# Previous failed runs often leave a 0-byte stub; allow replace.
		if ($existing.Length -gt 0) {
			throw ("Refusing to overwrite {0} ({1} bytes; pass -Force)." -f $outFile, $existing.Length)
		}
		Write-Warning ("Replacing empty capture stub: {0}" -f $outFile)
		Remove-Item -LiteralPath $outFile -Force
	}

	Write-Host ''
	Write-Host ('======== Scenario {0} ({1}) ========' -f $Key, $meta.Title)

	$proc = $null
	try {
		if ($meta.ServiceRestart) {
			Write-Host $meta.Prompt
			Write-Host 'Press Enter to stop EinkSvr and START capture...' -ForegroundColor Cyan
			[void](Read-Host)

			Stop-EinkStack
			$proc = Start-UsbCapture -OutputFile $outFile -Target $Target
			Write-Host ("Settling {0}s with stack stopped..." -f $SettleBeforeSec)
			Start-Sleep -Seconds $SettleBeforeSec

			Start-EinkStack
			$after = [Math]::Max($SettleAfterSec, 20)
			Write-Host ("Capturing {0}s after EinkSvr start (do not touch E-Ink)..." -f $after)
			Start-Sleep -Seconds $after
		} elseif ($meta.Idle) {
			Write-Host ("Idle capture {0}s - do not touch the E-Ink." -f $IdleSeconds)
			$proc = Start-UsbCapture -OutputFile $outFile -Target $Target
			Start-Sleep -Seconds $IdleSeconds
		} else {
			Write-Host $meta.Prompt
			Write-Host ''
			Write-Host 'Press Enter to START capture (then wait for the action prompt)...' -ForegroundColor Cyan
			[void](Read-Host)

			$proc = Start-UsbCapture -OutputFile $outFile -Target $Target
			Write-Host ("Settling {0}s..." -f $SettleBeforeSec)
			Start-Sleep -Seconds $SettleBeforeSec

			Write-Host ''
			Write-Host '>>> Perform the Homebar action NOW <<<' -ForegroundColor Yellow
			Write-Host $meta.Prompt
			[void](Read-Host)

			Write-Host ("Settling {0}s after action..." -f $SettleAfterSec)
			Start-Sleep -Seconds $SettleAfterSec
		}
	} finally {
		Stop-UsbCapture -Process $proc
	}

	if (-not (Test-Path -LiteralPath $outFile)) {
		throw "Capture finished but file missing: $outFile"
	}
	$size = (Get-Item -LiteralPath $outFile).Length
	Write-Note ("{0}  bytes={1}  iface={2}  devices={3}" -f `
		$meta.File, $size, $Target.Interface, $(if ($Target.DeviceAddress) { $Target.DeviceAddress } else { 'ALL' }))
	Write-Host ("Wrote {0} ({1} bytes)" -f $outFile, $size) -ForegroundColor Green
}

# --- main ---
Assert-Preflight
$target = Resolve-EinkCaptureTarget
Write-Host ("Capture target: iface={0} devices={1} all={2}" -f `
	$target.Interface, $target.DeviceAddress, $target.CaptureAll)

$keys = if ($Scenario -eq 'All') { @('B', 'C', 'D') } else { @($Scenario) }
foreach ($key in $keys) {
	Invoke-GuidedCapture -Key $key -Target $target
}

Write-Host ''
Write-Host "Done. Files in: $OutDir"
Write-Host 'Open in Wireshark with filter: usb.idVendor == 0x048d && usb.idProduct == 0x8951'
