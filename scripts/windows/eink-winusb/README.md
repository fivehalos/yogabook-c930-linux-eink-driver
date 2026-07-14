# Windows WinUSB ITE8951 tool (no EinkSvr)

MI_00 (`ITE T-CON`) already uses **WinUSB** with interface GUID
`{F0CFF988-E528-4B4A-8CE8-2F70DA273649}`.

Wire protocol (validated): [docs/PROTOCOL_WINDOWS.md](../../../docs/PROTOCOL_WINDOWS.md).

## Build

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows\eink-winusb\build.ps1
```

## Run (EinkSvr must be stopped)

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Set-EinkSvrAutostart.ps1 -Mode Manual -StopNow

.\scripts\windows\eink-winusb\EinkWinUsb.exe info
.\scripts\windows\eink-winusb\EinkWinUsb.exe scenario-get
.\scripts\windows\eink-winusb\EinkWinUsb.exe scenario-set 1
.\scripts\windows\eink-winusb\EinkWinUsb.exe pen-mouse
.\scripts\windows\eink-winusb\EinkWinUsb.exe scenario-get
```

| Command | Meaning |
|---------|---------|
| `info` | GET_SYS |
| `scenario-get` | `0xA6` GET — KB is byte1=`1` |
| `scenario-set N` | try `<<24` / low / arg1 encodings |
| `pen-mouse` | leave KB: optional `0xB3`+`0xA9`, then `0xA6` `0x03000000` |
| `mt-replay` | replay E-multitouch early `0xB3`/`0xA9`/`0xA6 0x01030100` |
| `fill [white\|black\|gray\|0xNN]` | full-panel LD_IMG (`0xA8`) + blit (`0x94`) |
| `stripes` | 64px black/white bands (ghost check) |
| `blit-test` | interactive white → black → stripes |

### Multitouch mode + blit (what stays / goes)

EinkSvr holds MI_00 while Homebar is up — stop it before WinUSB blits:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Test-MtBlit.ps1
```

That script: Homebar MT first → stop EinkSvr → `fill white` / `black` / `stripes` with pauses so you can note ghosts vs clears.

## If bulk I/O times out (121 / wait 258)

Almost always a missed **USBS** drain. Rebuild current tool (drains on exchange +
on open). If still wedged: reboot Windows, keep EinkSvr stopped, retry.
