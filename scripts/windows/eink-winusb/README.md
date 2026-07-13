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

Success: `scenario-get` ≠ `1` (usually `0`); E-Ink touch does not type into Notepad.
Homebar UI will not appear (Lenovo userspace).

## If bulk I/O times out (121 / wait 258)

Almost always a missed **USBS** drain. Rebuild current tool (drains on exchange +
on open). If still wedged: reboot Windows, keep EinkSvr stopped, retry.
