<#
.SYNOPSIS
  Controlled reg delta: our init-s → dump → Homebar MT switch → dump → diff.

.DESCRIPTION
  EinkSvr is NOT trusted as a quiet bring-up. We:
    1. Stop EinkSvr / EInk* and take MI_00.
    2. Run our S-style enable clone (init-s).
    3. Dump MMIO (0x83 only).
    4. Release the device and prompt you to Start EinkSvr, Homebar → pen/touchpad,
       confirm 2-finger, then hard-stop EinkSvr again.
    5. Re-open MI_00, dump again, diff.

  Run elevated if service stop/start needs it.
#>
[CmdletBinding()]
param(
	[string]$OutDir = '',
	[string]$RegBase = '0x18001100',
	[int]$RegWords = 0x80,
	[switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exeDir = Join-Path $here 'eink-winusb'
$exe = Join-Path $exeDir 'EinkWinUsb.exe'
$svcScript = Join-Path $here 'Set-EinkSvrAutostart.ps1'

if (-not $OutDir) {
	$repo = Split-Path -Parent (Split-Path -Parent $here) # …/scripts/windows → repo
	$OutDir = Join-Path $repo 'win-captures\reg-delta'
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$before = Join-Path $OutDir 'regs-before.txt'
$after = Join-Path $OutDir 'regs-after.txt'
$diffOut = Join-Path $OutDir 'regs-diff.txt'

function Assert-Admin {
	$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
	$principal = New-Object Security.Principal.WindowsPrincipal($identity)
	if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
		Write-Warning 'Not elevated — Stop-Service / Start-Service may fail. Re-run as Admin if needed.'
	}
}

function Stop-EinkStack {
	Write-Host '=== Stop EinkSvr stack (release MI_00) ===' -ForegroundColor Cyan
	& $svcScript -Mode Manual -StopNow
	Start-Sleep -Seconds 2
	Get-Process -Name 'EInk*','Eink*' -ErrorAction SilentlyContinue |
		ForEach-Object { Write-Host ("  kill {0} pid={1}" -f $_.ProcessName, $_.Id); Stop-Process $_ -Force -ErrorAction SilentlyContinue }
	Start-Sleep -Seconds 1
}

function Invoke-Eink {
	param([Parameter(ValueFromRemainingArguments = $true)][string[]]$EinkArgs)
	& $exe @EinkArgs
	if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 2 -and $LASTEXITCODE -ne 3) {
		throw "EinkWinUsb failed ($LASTEXITCODE): $($EinkArgs -join ' ')"
	}
	return $LASTEXITCODE
}

Assert-Admin

if (-not $SkipBuild) {
	Write-Host 'Building EinkWinUsb...' -ForegroundColor Cyan
	& (Join-Path $exeDir 'build.ps1')
}
if (-not (Test-Path $exe)) { throw "Missing $exe" }

Write-Host @"

Plan:
  1. Stop Lenovo stack
  2. OUR init-s (S enable clone — not live EinkSvr)
  3. reg-dump BEFORE
  4. YOU: start EinkSvr → Homebar pen/touchpad → 2-finger OK → stop EinkSvr
  5. reg-dump AFTER + diff

OutDir: $OutDir
Scan:   base=$RegBase words=$RegWords (0x83 only)

"@

Stop-EinkStack

Write-Host '=== init-s ===' -ForegroundColor Cyan
Invoke-Eink init-s | Out-Null

Write-Host '=== reg-dump BEFORE ===' -ForegroundColor Cyan
Invoke-Eink reg-dump $before $RegBase $RegWords | Out-Null
Write-Host "Wrote $before"

Write-Host @'

=== RELEASE / SWITCH ===
WinUSB handle is closed. Now:

  1. Start-Service EinkSvr   (or Set-EinkSvrAutostart.ps1 -Mode Manual -StartNow)
  2. Wait for Homebar on E-Ink
  3. Tap pen / touchpad / mouse (NOT keyboard)
  4. Confirm 2-finger on E-Ink (Notepad on LCD should stay clean)
  5. Stop EinkSvr hard again (services.msc or this script will do it after Enter)

Press Enter when MT is armed and EinkSvr is STOPPED...
'@ -ForegroundColor Yellow
[void](Read-Host)

Stop-EinkStack

Write-Host '=== reg-dump AFTER ===' -ForegroundColor Cyan
$code = Invoke-Eink reg-dump $after $RegBase $RegWords
Write-Host "Wrote $after"

Write-Host '=== scenario-get (want 3 if MT latched) ===' -ForegroundColor Cyan
Invoke-Eink scenario-get | Out-Null

Write-Host '=== reg-diff ===' -ForegroundColor Cyan
& $exe reg-diff $before $after 2>&1 | Tee-Object -FilePath $diffOut
Write-Host "Diff log: $diffOut"
Write-Host 'Done. Empty diff ⇒ scan window too narrow or MT state not in these regs.'
