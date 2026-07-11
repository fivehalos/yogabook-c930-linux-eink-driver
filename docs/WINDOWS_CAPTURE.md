# Windows USB capture (reverse engineering)

**Purpose:** Record the Lenovo coordinator’s USB commands when switching E-Ink
input modes. Windows is a **microscope**, not the Linux solution. Replay the
captured sequence in `kernel/eink_drm/protocol/ite8951_usb.c`.

**Problem on Linux:** Firmware stays in keyboard scenario (`0xA6` GET returns
`1`). Touch on the panel phantom-types into apps. We need the **full transition**
Windows uses for pen-mouse (scenario `3`).

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
| Empty `win-captures/` folder | Store `.pcapng` + notes |

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
| Scenario | `0xA6` — exact SET bytes (not GET `address=0`) |
| TP areas | `0xAF` |
| Handwriting / dynamic flags | `0xAC`, `0xB3` |
| Display routing | `display_cfg` `0x18001138` |
| Legacy DWORDs | `0x00040000`, `0x01010000`, `0x00000000` |

Three-packet pattern: control OUT → data IN/OUT → status IN (see
`reference/linux-2019/usb-protocol.md`).

---

## Export back to Linux

```text
win-captures/
  C-penmouse.pcapng      ← required
  B-keyboard.pcapng      ← for diff
  capture-notes.txt
```

**Optional (static RE without another boot):** copy from Windows install:

```text
EinkSvr.exe
itetcon.dll
EinkIteAPI.dll
```

Search under `C:\Program Files\Lenovo\` or `Program Files (x86)\Lenovo\`.

---

## Success on Windows (confirm before leaving)

- Pen-mouse mode: touch on E-Ink does **not** type in Notepad.
- Keyboard mode: touch **does** type in Notepad.

If pen-mouse already fails on Windows, fix the Lenovo stack before capturing.

---

## Back on Linux

1. Reboot to Linux.
2. Diff B → C (Wireshark + `protocol.lua`).
3. Implement sequence in `ite8951_request_input_scenario()` /
   `ite8951_try_set_coordinator_scenario()`.
4. Retest: `journalctl -k | grep -i scenario` should show `3`, phantom typing
   stops, `eink-touchpad` sees touch HID `0x90`.

See [BLUEPRINT.md](BLUEPRINT.md) for driver bring-up commands.
