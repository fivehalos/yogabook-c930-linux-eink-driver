# Windows agent brief — arm HID 0x0c from cold (not just GET=3)

**Carry this to the Windows RE session.**  
Related: [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md), [WINDOWS_CAPTURE.md](WINDOWS_CAPTURE.md),  
earlier: [WINDOWS_MULTITOUCH_BRIEF.md](WINDOWS_MULTITOUCH_BRIEF.md).

---

## Linux status (Jul 2026) — what already works / what doesn’t

| Working on Linux | Broken / incomplete |
|------------------|---------------------|
| `mt_latch` / E early `0xB3`/`0xA9`/`0xA6 addr 0x01030100` | HID report **`0x0c` never streams** (hidraw digitizer iface quiet) |
| GET byte1 **`1` → `3`** from firmware KB (cold) | Firmware ABS_MT touchpad (`event8`) also **silent** |
| Owner DRM blit while GET=`3` | Live touch is only HID **`0x90`** (single contact, iface 1) |
| Soft multi-finger heuristics on `0x90` | Real ≥2 simultaneous contacts |

**Conclusion:** Scenario **`3`** (pen-mouse / Wacom pass-through) is **necessary but not sufficient**.  
Linux proved GET=`3` alone ≠ multitouch. We need the USB recipe that **starts `0x0c` IN reports**, not only the latch that keeps GET=`3`.

Do **not** spend time re-proving:
- leave-KB `0x03000000` → GET=`0`
- `mt-replay` → GET=`3`
- owner `fill` / `stripes` while GET=`3`

Those are done.

---

## Mission

From a **clean starting point**, capture the sequence that makes **all** of these true at once:

1. Started in firmware **keyboard** (GET byte1 = **`1`**), or as cold as practical with EinkSvr **stopped** so WinUSB owns MI_00.
2. After the mockup / opcode sequence: GET = **`3`**.
3. Touch on E-Ink produces HID report ID **`0x0c`** with **≥2 contacts** (same class as `E-hid-0x85.txt`).
4. Capture covers **every bulk OUT** from “still no `0x0c`” → “`0x0c` live”, including anything **before** the first `0xA6 0x01030100` if Homebar isn’t used.

Optional but useful: confirm Notepad on LCD stays clean (not typing).

---

## Why the previous E capture wasn’t enough

`win-captures/E-usbc-ops.txt` + `E-hid-0x85.txt` show:

- Mode-entry on MI_00 and **already-live** `0x0c` traffic.
- **No `0xAF`** in that extract — TP areas / sensor enable may have happened **earlier** (Homebar / boot / earlier tap).
- Linux replay of that early subset gets GET=`3` but **silent** digitizer.

So we still need either:

- **Cold path:** KB (or EinkSvr-down) → **only** EinkWinUsb / known opcodes → **first** `0x0c` frames, or  
- **Diff path:** capture from “GET=`3` but no `0x0c`” → something → “`0x0c` live” (if you can create the silent-`3` state on Windows like Linux).

---

## Hardware / tools

- YogaBook C930 Windows dual-boot (not a VM).
- `048d:8951`, USBPcap + Wireshark (`protocol.lua` optional).
- `scripts/windows/eink-winusb/EinkWinUsb.exe` (`scenario-get`, `mt-replay`, `pen-mouse`, `stripes`).
- A way to see contact count: HID inspector, or dump EP like `E-hid-0x85.txt` (report id `0c`, byte2 = count).

Suggested filter: `usb.idVendor == 0x048d && usb.idProduct == 0x8951`.

---

## Procedure (try in order)

### Prep

1. Disable Fast Startup.
2. Prefer **EinkSvr stopped** for WinUSB-owned MI_00 unless a step explicitly needs Homebar.
3. `scenario-get` → record byte1 (want start **`1`** for cold KB).
4. Confirm **no** `0x0c` traffic yet (or note if something already streams).

### A — Cold mockup only (priority)

EinkSvr stopped, start in KB if possible:

```text
EinkWinUsb.exe scenario-get          # expect 1
# START CAPTURE
EinkWinUsb.exe mt-replay
EinkWinUsb.exe scenario-get          # expect 3
# 1 / 2 / 3 finger on E-Ink while capture runs
# STOP CAPTURE
```

**Pass for this brief:** GET=`3` **and** `0x0c` with count ≥ 2.  
**Fail:** GET=`3` but only silence / only `0x90`-class single contact (same as Linux today).

If `mt-replay` alone fails MT, try extending the replay toward full E early ops (`0xAE`/`0xAC`, `B3 0100/0003/0201`, GET_SYS, display_cfg RMW) **one add at a time** and note which step makes `0x0c` appear.

### B — Homebar then isolate (fallback)

If A never yields `0x0c`:

1. Start capture **before** Homebar pen/touchpad tap.
2. Homebar → pen/touchpad until 2-finger works.
3. Stop EinkSvr; keep capturing briefly; confirm `0x0c` still live + GET=`3`.
4. Extract **every** MI_00 OUT from capture start through first multi-contact `0x0c` — especially **`0xAF`**, structured/CDB `0xB3`, registers.

Goal: shortest opcode list that Linux can play without Homebar.

### C — Silent GET=3 → wake (if reproducible on Windows)

If you can get GET=`3` with **no** `0x0c` (Linux-like):

1. Capture.
2. Run candidate wake ops (AF flags, B3 bypass/pen-mouse encodings, etc.).
3. Stop when `0x0c` appears.

---

## What to extract for Linux

| Item | Why |
|------|-----|
| Full bulk OUT list for the **first** `0x0c` enable | Port into `ite8951_mt_mode_replay()` / arm helper |
| Every **`0xAF`** (CDB vs payload, flag = TOUCH_ONLY / TOUCH_PEN / …) | E extract had none |
| **`0xB3`**: CDB-inline vs OUT payload; bypass vs pen-mouse encoding | Linux structured B3 + TOUCH_ONLY only woke `0x90` |
| HID path: iface / EP / report id | Confirm still `0x0c` on digitizer (Linux iface 3 / hidraw with `0x0c` in descriptor) |
| Explicit: HID SET_REPORT / SET_FEATURE on non-bulk ifaces? | Only if present; currently assumed unused |

Also answer in notes:

1. Did **`mt-replay` alone** from GET=`1` produce live `0x0c`, or only GET=`3`?
2. Did owner blit (`stripes`) stay OK while `0x0c` streamed?
3. Exact filenames under `win-captures/`.

---

## Success criteria (strict)

| Check | Pass |
|-------|------|
| Start state | GET=`1` (or documented cold) / no `0x0c` yet |
| After sequence | GET=`3` |
| 2 fingers together | Report `0x0c`, contact count ≥ 2 |
| Capture | MI_00 from before enable through first multi-contact frames |
| Notes | Which step first made `0x0c` appear |

**Fail:** GET=`3` with quiet digitizer (Linux already has that).  
**Fail:** only single-point `0x90`.

---

## Return checklist

- [ ] pcapng (or extract) covering cold → first live `0x0c`
- [ ] Opcode diff / list (esp. `0xAF` / `0xB3` variants)
- [ ] `scenario-get` before/after
- [ ] Confirmed 2+ contacts on `0x0c`
- [ ] One line: “`mt-replay` alone yes/no for live MT”
- [ ] Whether any HID control transfers (non–MI_00) were required

When that lands, Linux work is: extend `ite8951_mt_mode_replay()` / arm path to that recipe, **drop** the arming that only enables `0x90`, then `eink-touchpad` can prefer real `0x0c` again.
