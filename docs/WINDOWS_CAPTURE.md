# Windows USB capture (reverse engineering)

**Purpose:** Record the Lenovo coordinator’s USB commands when switching E-Ink
input modes. Windows is a **microscope**, not the Linux solution. Replay the
captured sequence in `kernel/eink_drm/protocol/ite8951_usb.c`.

**Problem on Linux:** Firmware stays in keyboard scenario (`0xA6` GET byte1 =
`1`). Touch on the panel phantom-types into apps.

**Validated on Windows (no EinkSvr):** leave keyboard with `0xA6` address
`0x03000000`; GET then reports **`0`** (not `3`), and typing stops. Full wire
notes: [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md).

---

## Dual-boot on the C930 — not a VM

The panel is built-in USB `048d:8951` (composite, four interfaces). A VM cannot
reliably pass it through. Boot **Windows 10 on the YogaBook** for captures.

---

## Before you boot Windows (on Linux)

Copy to a USB stick or shared NTFS partition:

| File | Why |
|------|-----|
| `reference/linux-2019/wireshark/protocol.lua` | Dissector (after `./scripts/fetch-references.sh --linux`) |
| Empty `win-captures/` folder | Store `.pcap` / `.pcapng` + notes |

Fetch the Linux reference if needed:

```bash
./scripts/fetch-references.sh --linux
```

---

## One-time Windows setup

1. **Dual-boot** Windows 10 on the C930 (shrink partition or second disk).
2. **Disable Fast Startup** (Power options → “Choose what power buttons do”).
3. Install **Lenovo E-Ink stack** — Vantage / OEM software so these exist:
   - Device Manager: `048d:8951`
   - `services.msc`: **EinkSvr** running
   - Registry: `HKLM\SOFTWARE\Lenovo\EinkSDK`
4. Install **Wireshark** + **USBPcap** ([wireshark.org](https://www.wireshark.org/), [desowin.org/usbpcap](https://desowin.org/usbpcap/)).
5. Copy `protocol.lua` to `%APPDATA%\Wireshark\plugins\`, restart Wireshark.

**Sanity:** E-Ink Homebar visible after login. Open **Notepad** on the main LCD
(to test phantom typing).

---

## Capture procedure

For each capture: **Start USBPcap → wait 5 s → act → wait 10 s → Stop.**  
Use display filter: `usb.idVendor == 0x048d && usb.idProduct == 0x8951`

Keep `capture-notes.txt` with filename, time, and what you did.

| File | Action |
|------|--------|
| `A-coldboot.pcapng` | Reboot, login, don’t touch E-Ink ~30 s |
| `B-keyboard.pcapng` | Homebar **keyboard** → type on E-Ink → chars in Notepad |
| **`C-penmouse.pcapng`** | Homebar **pen / touchpad** → drag on E-Ink → **no** Notepad input |
| `D-roundtrip.pcapng` | keyboard → pen-mouse → keyboard → pen-mouse |

**`C-penmouse.pcapng` is the priority file.**

---

## What we need from the trace

Compare **B vs C**. Every bulk **OUT** that changes on the Homebar tap is a
candidate for Linux.

| Item | Opcode / register |
|------|-------------------|
| Scenario | `0xA6` — SET uses **`address = scenario << 24`**; leave-KB = `0x03000000` (GET → `0`) |
| TP areas | `0xAF` |
| Handwriting / dynamic flags | `0xAC`, `0xB3` (Windows often CDB-inline) |
| Display routing | `display_cfg` `0x18001138` |
| Legacy DWORDs | `0x00040000`, `0x01010000`, `0x00000000` |

Three-packet pattern: control OUT → data IN/OUT → **always drain** status IN
(see [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md)).

---

## Export back to Linux

```text
win-captures/
  C-penmouse.pcap        ← required (USBPcapCMD; Wireshark opens .pcap)
  B-keyboard.pcap        ← for diff
  A-coldboot.pcap        ← boot / Homebar enable (recommended)
  capture-notes.txt
```

Manual Wireshark captures may still use `.pcapng`; either format is fine.

**Optional (static RE without another boot):** copy from Windows install:

```text
EinkSvr.exe
itetcon.dll
EinkIteAPI.dll
```

Search under `C:\Program Files\Lenovo\` or `Program Files (x86)\Lenovo\`.

---

## Success on Windows (confirm before leaving)

- After leave-KB (Homebar pen/touchpad **or** `EinkWinUsb.exe pen-mouse`):
  touch on E-Ink does **not** type in Notepad; `scenario-get` is **not** `1`
  (typically `0`).
- Keyboard mode: touch **does** type in Notepad; GET byte1 = `1`.

If pen-mouse already fails on Windows, fix the Lenovo stack before capturing.

---

## Back on Linux

1. Reboot to Linux (optional once Windows leave-KB is proven).
2. Port [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md) into
   `ite8951_try_set_coordinator_scenario()` / `ite8951_request_input_scenario()`:
   prefer `address = scenario << 24`; treat GET=`0` after leave-KB as success.
3. Retest: phantom typing stops; `journalctl -k | grep -i scenario` should show
   non-`1` after draw entry.

See [BLUEPRINT.md](BLUEPRINT.md) for driver bring-up commands.

---

## Automated capture (Windows)

Guided helpers under `scripts/windows/` drive **USBPcapCMD** with the same
timing as above. **Homebar taps stay manual** — the script prompts you.

Run **elevated** PowerShell from the repo:

```powershell
# Multitouch leave-KB path (PRIORITY when hunting ABS_MT / ≥2 contacts)
# See docs/WINDOWS_MULTITOUCH_BRIEF.md
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Capture-EinkUsb.ps1 -Scenario E -Force

# Priority pen-mouse leave-KB capture (scenario only; may be single-contact)
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Capture-EinkUsb.ps1 -Scenario C

# EinkSvr restart = enable/init traffic (better than warm idle "A")
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Capture-EinkUsb.ps1 -Scenario S -Force

# Keyboard + pen-mouse + roundtrip
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Capture-EinkUsb.ps1 -Scenario All -Force

# Coldboot / Homebar-enable traffic after next login
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Capture-EinkColdboot.ps1 -Arm
# then reboot, log in, do not touch E-Ink for ~100s

# Or smoke-test idle capture without rebooting (misses true boot bring-up)
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Capture-EinkColdboot.ps1 -CaptureNow -Force
```

If you prefer a session-wide bypass in an elevated window:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\windows\Capture-EinkUsb.ps1 -Scenario C
```

Output is `.pcap` (USBPcapCMD native; Wireshark opens it). Files land in
`win-captures/` with lines appended to `capture-notes.txt`.

Preflight checks: USBPcapCMD present, **EinkSvr** Running, warns if
**EInkHomebar** is missing.

### USBPcap “Couldn't open device” / no `\\.\USBPcapN`

`Test-Path \\.\USBPcap1` often returns **False** even when capture works —
ignore it. Prefer:

```powershell
& "C:\Program Files\USBPcap\USBPcapCMD.exe" --extcap-interfaces
```

The capture script uses that same listing (not `Test-Path`).

This C930 has a **USB 3.0** root hub (`ROOT_HUB30`). USBPcap needs a one-time
HW-ID scan, then a reboot:

```powershell
# elevated PowerShell
& "C:\Program Files\USBPcap\USBPcapCMD.exe" -I
# reboot, then:
& "C:\Program Files\USBPcap\USBPcapCMD.exe" --extcap-interfaces
```

You want `interface {value=\\.\USBPcapN}{display=USBPcapN}` lines. Also check
Wireshark → Capture → Options for `USBPcapN`.

If still empty: reinstall `USBPcapSetup-1.5.4.0.exe`, run `-I` again, reboot.

### Isolate from Lenovo stack (autostart off)

To reboot **without** EinkSvr/Homebar and test your own USB bring-up later:

```powershell
# elevated, from repo root
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Set-EinkSvrAutostart.ps1 -Mode Disabled -StopNow
# reboot — E-Ink Homebar should be gone; firmware keyboard HID may remain

# restore Lenovo default when done
powershell -ExecutionPolicy Bypass -File .\scripts\windows\Set-EinkSvrAutostart.ps1 -Mode Automatic -StartNow
```

Use `-Mode Manual` if you still want Scenario S (`Start-Service`) without boot autostart.

**Important:** Homebar buttons are Lenovo UI (`EInkHomebar`), not firmware chrome.
Turning EinkSvr off removes that UI. “Our own work” means replaying the USB
init/scenario sequence from `S`/`C` (Linux `eink_drm` / a future WinUSB tool),
not regenerating Lenovo’s Homebar binary.
