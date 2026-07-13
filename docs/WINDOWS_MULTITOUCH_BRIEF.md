# Windows agent brief — find multitouch leave-KB path

**Carry this to the Windows RE session.**  
Related: [WINDOWS_CAPTURE.md](WINDOWS_CAPTURE.md), [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md).

---

## Context (Linux, Jul 2026)

We already have on Linux:

| Working | Broken / incomplete |
|---------|---------------------|
| Leave firmware keyboard via `0xA6` SET addr `0x03000000` (GET byte1 → `0`) | True multi-finger input |
| DRM compositor output on E-Ink | `eink-touchpad` uses HID report `0x90` — **single contact only** (`touch[1]` always) |
| Relative pointer + tap clicks (soft heuristics) | Kernel ABS_MT digitizer (`event9`, 10 slots) advertised but **silent** under our draw touch route |

### A/B on Linux (already done)

1. **`touch_userspace_config=1`** (default): leave-KB + `0xAC`/`0xB3`/`SET_TP_AREA` `TOUCH_PEN=0x03` → HID `0x90` **live**, one point. ABS_MT **dead**.
2. **`touch_userspace_config=0`**: leave-KB, **skip** that touch redirect → ABS_MT still **dead**, `0x90` also **dead**. Touch goes nowhere.

**Conclusion:** Leave-KB alone is not enough. We need a **third** Windows path:
firmware keyboard **off** + **multicontact digitizer** still reporting.

Do **not** spend time re-proving leave-KB (`0x03000000`) unless it regresses. That is solved.

---

## Mission

Find a Windows input mode where **all** of these hold:

1. Touch on E-Ink does **not** type into Notepad (not scenario keyboard / GET ≠ `1`).
2. Digitzer reports **≥2 simultaneous contacts** (2- and 3-finger chord).
3. Capture the USB opcode sequence (`0xA6` / `0xB3` / `0xAF` / related) that entered that mode.

That recipe becomes the Linux `eink_drm` touch routing for NKRO keyboard + real touchpad.

---

## Hardware / tools

- YogaBook C930 dual-boot Windows (not a VM).
- Device `048d:8951`.
- Wireshark + USBPcap; optional `protocol.lua` from the repo.
- Notepad on LCD (phantom-key test).
- A way to see multitouch contacts, e.g.:
  - Microsoft “Paint” / any touch app that shows multi-finger, or
  - HID / digitizer inspector, or
  - Device Manager → find active HID digitizer / touchpad under the ITE composite while testing.

Repo helper still useful: `scripts/windows/eink-winusb/EinkWinUsb.exe` (`pen-mouse`, `scenario-get`).

---

## Success criteria (strict)

| Check | Pass |
|-------|------|
| Notepad while touching E-Ink | **No** characters |
| `scenario-get` / `0xA6` GET byte1 | **≠ 1** (expect `0` after leave-KB) |
| 1 finger | Contact visible |
| 2 fingers down together | **Two** contacts / slots / tracking IDs |
| 3 fingers | **Three** contacts |
| Capture | USB pcap covering mode entry + the 1/2/3 finger sequence |

**Fail** if only one contact ever appears (that is the same class as Linux `0x90`).  
**Fail** if Notepad types (firmware keyboard).  
**Fail** if leave-KB works but touch is completely dead (same as Linux `touch_userspace_config=0`).

---

## Procedure

### Prep

1. Disable Fast Startup.
2. Install Lenovo E-Ink stack so Homebar / EinkSvr exist (or use WinUSB tool if you already know leave-KB).
3. Open Notepad on the **LCD**.
4. Start USBPcap with filter: `usb.idVendor == 0x048d && usb.idProduct == 0x8951`.

### Find the mode (try in order)

For each candidate mode: **Start capture → switch mode → wait 3 s → 1/2/3 finger on E-Ink → Stop.**  
Write what you did in `capture-notes.txt`.

| # | Candidate | How | Likely result (hypothesis) |
|---|-----------|-----|----------------------------|
| A | Homebar **pen / touchpad / mouse** | UI toggle | Best guess for multicontact + no Notepad |
| B | `EinkWinUsb.exe pen-mouse` only | No Homebar | Leave-KB; may or may not feed MT |
| C | Same as A/B then use Reader / ink / draw | Extra `0xB3`/`0xAF` | May collapse to single-point `0x90`-style |
| D | Homebar **keyboard** | Control | Multicontact as keys — **reject** for this mission |

Focus on **A**. If A shows real 2/3 finger contacts and no Notepad → **that is the file we need.**

Suggested filenames:

- `E-multitouch-penmouse.pcapng` — Homebar pen/touchpad + 1/2/3 finger (priority)
- `E-notes.txt` — scenario GET, Nest/Homebar state, which HID device showed MT
- `F-ink-or-draw.pcapng` — if you enter ink/draw after leave-KB, note if MT collapses to 1 contact

### Parallel observations (not USB)

While 2/3 fingers are down, note:

- Device Manager / HID: **which interface** is producing the multitouch (touchpad vs digitizer vs mouse).
- Contact count UI or inspector: slots = ?

Linux will map that to `event8` (touchpad) / `event9` (direct digitizer) / or a hidraw report ID other than single-point `0x90`.

---

## What to extract for Linux (hand back)

From the **passing** capture, list every bulk OUT that differs from cold keyboard, especially:

| Opcode | Why |
|--------|-----|
| `0xA6` SCENARIO | Address + GET byte1 after |
| `0xB3` DYNAMICSETTING | Full payload / CDB-inline args (`uc_bypass_flag`, `uc_pen_mouse_flag`, …) |
| `0xAF` SET_TP_AREA | Rectangles + **flag** bytes (not only `0x03` TOUCH_PEN / `0x00` NO_REPORT) |
| `0xAC` handwriting | If present |
| Anything else on mode switch | Don’t drop unknown OUT |

Also answer explicitly:

1. Does this mode use classic **ABS_MT / Windows digitizer**, or a vendor HID report with contact count > 1?
2. Does entering “draw / ink / reader” later **kill** multitouch (collapse to 1 point)? Timestamp that transition if so.
3. Can we keep **display updates** under Linux ownership while staying in the multitouch leave-KB mode? (If Windows can’t draw and Linux can’t, note that — we may need dual routing.)

---

## Do not optimize for

- Re-validating `0x03000000` leave-KB alone (done).
- Soft multi-touch heuristics on single-point `0x90` (Linux hack only; not for NKRO keyboard).
- Firmware keyboard zone typing as a “multitouch” solution.

---

## Return checklist for the Linux agent

- [ ] `E-multitouch-penmouse.pcapng` (or equivalent) attached / copied to `win-captures/`
- [ ] Notes: GET scenario, Homebar mode name, Notepad = clean
- [ ] Confirmed **2+** and **3** contacts observed
- [ ] Which Windows device / HID path carried MT
- [ ] Diff summary: `0xA6` / `0xB3` / `0xAF` (+ others) vs keyboard / vs ink
- [ ] Whether ink/draw collapses MT to one contact

When that lands, Linux work is: port that routing into `ite8951_detach_keyboard_input()` / draw-input path so ABS_MT (or multi-contact HID) stays alive with DRM, then drive `eink-touchpad` / `einkd` from real contacts — not `0x90` clustering.
