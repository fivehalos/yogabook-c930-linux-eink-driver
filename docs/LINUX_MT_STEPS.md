# Linux steps — multitouch (Windows RE handoff)

**Audience:** Linux agent / developer on the C930.  
**Last updated:** 2026-07-13 (end of Windows dual-boot session).  
**Sources:** [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md), `win-captures/E-usbc-ops.txt`, `E-hid-0x85.txt`,  
`scripts/windows/eink-winusb/EinkWinUsb.exe` (`mt-enter`, `fill`, `stripes`).

Shorter notes: [WINDOWS_MULTITOUCH_BRIEF.md](WINDOWS_MULTITOUCH_BRIEF.md).

---

## 1. Situation (truth as of today)

### Already working on Linux

| Piece | How |
|-------|-----|
| DRM / compositor on E-Ink | Owner `0xA8` + `0x94` at `0x00382f30` |
| Stop firmware KB typing | `0xA6` addr **`0x03000000`** → GET byte1 **`0`** |
| Single-point draw touch | Then `0xAC` / payload-`0xB3` / `0xAF TOUCH_PEN=0x03` → HID **`0x90`** (1 contact) |

That is **not** multitouch.

### Proven on Windows (keep these)

1. **Homebar** pen/touchpad/mouse → real **1/2/3** contacts on HID report **`0x0c`** (`E-hid-0x85.txt`: byte2 = count).
2. While that mode is live, GET byte1 is **`3`** (not `0`).
3. After Homebar has armed MT: hard-stop EinkSvr → owner **`fill`/`stripes`** still work (sharp); GET can stay **`3`**. Display ownership ≠ needing Lenovo to paint.
4. Parallel **`0x90`** can exist on another iface; **MT success = live `0x0c`**, not soft chords on `0x90`.

### Corrected: cold `mt-enter` alone is incomplete

Fresh reboot, **no EinkSvr / no Homebar**, then `EinkWinUsb.exe mt-enter` (full E ops 1–20):

| Check | Result |
|-------|--------|
| Before | GET=`1` (KB) |
| After | GET=`3` |
| Notepad / phantom KB | **Off** |
| Multitouch / `0x0c` traces | **Not observed** |

**Conclusion:** GET=`3` means “left keyboard / pen-mouse scenario,” **not** “digitizer streaming.”  
Homebar (or equivalent bring-up we have not fully isolated) still **arms** HID `0x0c`. Replaying only the early E USBC block is not enough for cold MT.

### Two non-KB modes

| Name | Enter | GET | Touch | Notes |
|------|-------|-----|-------|-------|
| Bare leave-KB | `0xA6` **`0x03000000`** | **`0`** | **`0x90`** 1 pt | Linux today |
| Scenario-3 leave | E / `mt-enter` cold | **`3`** | Often **silent** until Homebar-class arm | Leave-typing only if no `0x0c` |
| **Armed MT** | Homebar pen/touchpad (+ enable stack) | **`3`** | **`0x0c`** N contacts | Real multitouch |

---

## 2. Goals (product)

1. E-Ink shows our UI (DRM / custom board blit) — not Lenovo chrome after handoff.
2. Firmware not typing into the LCD session (GET ≠ `1`).
3. Real multitouch → `eink-touchpad` → **uinput** (parse `0x0c`).
4. Longer term: NKRO on LCD keyboard + E-Ink as touchpad.

---

## 3. Primary path (still preferred when possible)

Isolate the minimal USB that **both** sets GET=`3` **and** starts `0x0c` without a GUI Homebar. Candidates: `S-einksvr-restart.pcap` enable/init + E mode entry; diff vs cold `mt-enter`.

Until that exists, **do not** tell Linux “GET=`3` from `mt-enter` = MT done.”

Opcode table for the **leave / scenario** half (E ops 1–20) remains in §6 — useful, but **insufficient alone**.

---

## 4. Fallback plan (ship path if cold arm stays blocked)

**Idea:** Clone enough of the **EinkSvr / Homebar bring-up** that the user (or we) can enter MT the same way Windows does, then **take over display + input** without staying in Lenovo’s compositor forever.

### Planned flow

```
1. Bring-up  — Replay / clone EinkSvr enable + Homebar-class init
                (USB from S-einksvr-restart + whatever arms HID 0x0c).
                Optionally show a minimal scenario / “home bar” so the user
                can tap pen/touchpad once if firmware still needs that gesture.
2. User MT   — User selects pen/touchpad (or we SET the same sequence Homebar uses
                after UI click). Confirm 0x0c live (2/3 finger).
3. Handoff   — Our stack owns the panel:
                • DRM / custom board blit (0xA8/0x94) — proven with GET=3 after stop
                • Do NOT run TOUCH_PEN draw routing that collapses to 0x90
4. Touchpad  — hidraw 0x0c → parse contacts → uinput (eink-touchpad)
5. Optional  — Hide/stop Lenovo UI processes once MT is armed and we own blits
```

### Why this is acceptable

- We **already** know: after Homebar arms MT, **owner blit works** and GET can stay `3`.
- We **already** have a `0x0c` parser sketch in `userspace/eink-touchpad/touch_parse.c`.
- Cloning bring-up + one user MT tap is cheaper than blocking on a fully cold silent digitizer.

### What to clone / reverse next (Windows or Linux)

| Capture / area | Why |
|----------------|-----|
| `S-einksvr-restart.pcap` | Cold enable / init before any Homebar paint |
| Homebar click → MT | Exact post-UI opcode delta beyond E ops 1–20 if any |
| Alive `0x0c` after stop | Re-confirm: start bar → MT → kill EinkSvr → still `0x0c`? |

### What not to do in the fallback

- Do not keep Lenovo drawing forever as the display server.
- Do not enable Linux `TOUCH_PEN` full-panel after MT is armed (kills `0x0c` class).
- Do not count soft `0x90` multitouch as success.

---

## 5. Linux work order (practical)

### Now

1. Keep DRM working.
2. Gate `reapply_draw_input` / `TOUCH_PEN` when experimenting with MT.
3. Implement / finish **`0x0c` → uinput** path (parser exists; auto-hid pick still prefers `0x90`).
4. Treat **fallback §4** as the default product strategy until cold arm is proven.

### When dual-booting Windows again

1. Start EinkSvr → Homebar → pen/touchpad → **confirm 2-finger**.
2. Capture USB from enable through that click if not already complete.
3. Hard-stop EinkSvr; confirm whether **`0x0c` still streams**; `scenario-get`; `fill`/`stripes`.
4. Diff vs cold `mt-enter` (GET=`3`, no MT).

### Cold USB table (scenario leave only)

See §6. WinUSB: `mt-enter` then `scenario-get` → expect `3`. **Also** require hid/raw proof of `0x0c` before calling it MT.

---

## 6. E early USBC sequence (ops 1–20) — leave / scenario half

Framing: MI_00 OUT `0x02` / IN `0x81`; USBC → optional data → **drain USBS**.

Critical SET (not bare leave):

```
0xA6  address = 0x01030100
```

| # | Op | Address | Arg1 | Arg2 | Arg3 | Arg4 |
|---|-----|---------|------|------|------|------|
| 1 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | 0 |
| 2 | `0xB3` | 0 | `0x0101` | `0x0003` | `0x0301` | 0 |
| 3 | `0xA9` | 0 | `0x0200` | 0 | 0 | 0 |
| 4 | **`0xA6`** | **`0x01030100`** | 0 | 0 | 0 | 0 |
| 5–7 | `0x83`/`0x84` | panel / display_cfg | | | | |
| 8 | `0xAE` | 0 | `0x0100` | 0 | 0 | 0 |
| 9 | `0xAC` | 0 | 0 | 0 | 0 | 0 |
| 10 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | 0 |
| 11 | `0xB3` | 0 | `0x0100` | `0x0003` | `0x0301` | 0 |
| 12 | `0xA9` | 0 | `0x0200` | 0 | 0 | 0 |
| 13 | **`0xA6`** | **`0x01030100`** | 0 | 0 | 0 | 0 |
| 14 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | 0 |
| 15 | `0xB3` | 0 | `0x0100` | `0x0003` | `0x0201` | 0 |
| 16 | `0x80` | `0x38393531` | 1 | 2 | 0 | 0 |
| 17–19 | regs again | | | | | |
| 20 | **`0xA6` GET** | **`0`** | | | | | → byte1 **`3`** |

B3 here is **CDB-inline** (E expects short IN). Linux payload-`0xB3` bool struct is a different encoding.

**Do not** follow with `TOUCH_PEN` while validating `0x0c`.

---

## 7. HID `0x0c` layout

```
byte0: 0x0c
byte1: pad
byte2: contact_count
then each contact (7 B): tip_flags, contact_id, pad, x_le16, y_le16
```

Examples: `E-hid-0x85.txt`. Parser: `userspace/eink-touchpad/touch_parse.c`.

---

## 8. Acceptance checklist

Cold / USB-only:

- [ ] GET=`3` after entry
- [ ] No firmware KB typing  
- [ ] **`0x0c` read() with count ≥ 2** ← required for “MT”
- [ ] DRM blit still works

Fallback / Homebar-armed:

- [ ] User (or clone) entered pen/touchpad; `0x0c` live
- [ ] Our blits replace Lenovo UI
- [ ] `0x0c` → uinput works for 2/3 finger
- [ ] No `TOUCH_PEN` collapse after handoff

---

## 9. Files

| File | Why |
|------|-----|
| `docs/PROTOCOL_WINDOWS.md` | Wire notes; update with cold vs armed |
| `win-captures/E-*` | Mode + HID evidence |
| `win-captures/S-einksvr-restart.pcap` | Enable/init to clone for fallback |
| `scripts/windows/eink-winusb/` | `mt-enter`, `fill`, `stripes`, `scenario-get` |
| `kernel/.../ite8951_usb.c` | Today’s leave + TOUCH_PEN |
| `userspace/eink-touchpad/` | `0x0c` parse + uinput |

---

## 10. One-paragraph summary

Bare Linux leave (`0x03000000` → GET=`0` + `TOUCH_PEN`) yields single-point **`0x90`**. Homebar arms real multitouch on HID **`0x0c`** with GET=**`3`**; owner DRM blits can then take over. Cold **`mt-enter`** reaches GET=`3` and kills KB typing but **does not** by itself show `0x0c`. Until the missing arming opcodes are isolated, the **fallback** is: clone EinkSvr/Homebar bring-up → user enters MT → our blit + `0x0c`→uinput touchpad, without staying on Lenovo’s display stack.
