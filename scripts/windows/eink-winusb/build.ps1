$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $here 'EinkWinUsb.cs'
$out = Join-Path $here 'EinkWinUsb.exe'

$csc = Get-ChildItem "${env:WINDIR}\Microsoft.NET\Framework64\v4*\csc.exe" |
	Sort-Object FullName -Descending |
	Select-Object -First 1 -ExpandProperty FullName
if (-not $csc) {
	$csc = Get-ChildItem "${env:WINDIR}\Microsoft.NET\Framework\v4*\csc.exe" |
		Sort-Object FullName -Descending |
		Select-Object -First 1 -ExpandProperty FullName
}
if (-not $csc) {
	throw 'csc.exe not found (need .NET Framework 4.x)'
}

Write-Host "Using $csc"
& $csc /nologo /optimize+ /out:$out $src
if ($LASTEXITCODE -ne 0) {
	throw "csc failed: $LASTEXITCODE"
}
Write-Host "Built $out"
Write-Host 'Try:'
Write-Host "  $out scenario-get"
Write-Host "  $out pen-mouse"
