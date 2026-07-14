# Linux port brief — multitouch latch (GET=3 + HID 0x0c)

**Status: Windows validated Jul 2026.** This is no longer a hunt doc.

**Full method for the Linux agent:** [LINUX_MT_STEPS.md](LINUX_MT_STEPS.md)  
(use that file as the primary handoff — situation, cold vs armed MT, **fallback Homebar clone plan**, acceptance tests).

Wire detail: [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md). Extracts: `win-captures/E-usbc-ops.txt`, `E-hid-0x85.txt`.

**2026-07-13 correction:** cold `mt-enter` → GET=`3` + no KB, but **no `0x0c` MT** without Homebar-class arming. GET=`3` alone is not multitouch.

If Linux “can’t replicate,” assume it is still on the **bare leave-KB / draw-routing** path below — not an **armed** Homebar MT session.

---

## Two different non-keyboard modes (do not conflate)

| Mode | How entered | `0xA6` GET byte1 | Touch HID | Owner blit |
|------|-------------|------------------|-----------|------------|
| **Bare leave-KB** | `0xA6` addr `0x03000000` (± optional B3/A9) | **`0`** | Single-contact **`0x90`** | Yes |
| **MT / pen-mouse latch** | Homebar pen/touchpad → firmware latch | **`3`** | Multi-contact **`0x0c`** (EP interrupt / hidraw) | Yes |

Linux today mostly achieves row 1 (`touch_userspace_config=1` → leave + `0xAC`/`0xB3`/`0xAF TOUCH_PEN=0x03`). That is **solved leave-typing**, not multitouch.

**Success on Linux = GET byte1 stays `3` and hidraw streams report `0x0c` with contact count ≥2.**  
GET=`0` + live `0x90` is a **fail** for this mission (even if soft-chords work).

---

## What Windows proved (do not over-claim)

1. Homebar → pen/touchpad/mouse → **real 1/2/3 contacts** on report **`0x0c`** (`E-hid-0x85.txt`).
2. In that armed mode, GET byte1 is **`3`**.
3. After arming: owner blit with EinkSvr stopped works (sharp `stripes`).
4. Cold `mt-enter` (no Homebar) → GET=`3`, KB off, **`0x0c` not observed** — scenario leave ≠ digitizer arm.
5. Fallback product path: clone EinkSvr/Homebar bring-up → user MT → our blit + `0x0c`→uinput ([LINUX_MT_STEPS.md](LINUX_MT_STEPS.md) §4).

Parallel **`0x90`** can still appear; **MT is `0x0c`**, not ABS_MT under the draw route.

---

## Why Linux replication usually fails

Common mistakes:

1. **Treat any GET≠1 as success** — code accepts GET=`0` as “pen-mouse done.” That is bare leave, not MT.
2. **Send leave addr `0x03000000` only** — yields GET=`0`, never Homebar latch.
3. **After (or instead of) MT entry, run `ite8951_detach_keyboard_input()`** — `0xAC` + payload-`0xB3` + **`0xAF TOUCH_PEN=0x03`** re-routes touch to **single-point `0x90`**. Windows E **early** mode entry did **not** use that `0xAF` slot pattern; it used CDB-inline `0xB3`, `0xA9`, **`0xA6 0x01030100`**, then `0xAE`/`0xAC`, display_cfg, blits.
4. **Expect kernel ABS_MT (`event9`) to wake up** under the draw route — on Windows MT was vendor HID `0x0c`, not our silent digitizer node.
5. **Replay only `mt-replay` from cold keyboard without checking GET=`3` + live `0x0c`** — isolating the minimal cold subset is still open; Homebar may do more than the early USBC snippet before MT is armed.

Also note Windows B3 in E is often **CDB args only** (resp length 6), while Linux `set_dynamic_bool_values` sends a **struct payload**. Different encoding.

---

## Recipe to port (target)

### A. Enter / hold MT latch (priority)

From `E-usbc-ops.txt` ops 1–20 (before A8 blit storm), condensed:

```
0xB3  CDB args 0101 / 0003 / 0301      (expect short IN)
0xA9  arg1=0x0200
0xA6  address = 0x01030100             (NOT 0x03000000)
… reg reads 18001224 / 18001138 …
0x84  display_cfg write (as in E)
0xAE  arg1=0x0100
0xAC  (handwr clear)
0xB3  CDB args 0100 / 0003 / 0301
0xA9  0x0200
0xA6  address = 0x01030100             (again)
0xB3  CDB args 0100 / 0003 / 0201
0x80  GET_SYS / doorknock as in E
… display_cfg …
0xA6  GET address 0  → expect byte1 == 3
```

WinUSB helpers (Windows dual-boot only): `scenario-get`, `mt-replay`  
(`scripts/windows/eink-winusb/`).

**Do not** call the current `TOUCH_PEN` full-screen TP-area path after this if the goal is `0x0c`.

### B. Display

Keep existing DRM `0xA8`/`0x94` at `0x00382f30`. Validated with GET=`3`.

### C. Userspace input

- Prefer hidraw that advertises **report `0x0c`** when it is streaming (`eink-touchpad` already has a parser sketch).
- Layout (from Windows): `[0]=0x0c`, `[1]=pad`, `[2]=contact count`, then contacts tagged `05` + id + coords… (see `E-hid-0x85.txt` / `touch_parse.c`).
- Keep `0x90` path only as fallback — not the MT success path.

---

## Linux acceptance tests

| Check | Pass |
|-------|------|
| After input bring-up, `0xA6` GET byte1 | **`3`** (not `0`, not `1`) |
| `hidraw` / sniff | Report **`0x0c`**, count byte ≥ **2** with two fingers |
| Three fingers | count ≥ **3** |
| DRM frame | Still updates (owner blit) |
| Not firmware KB | No phantom typing into a focused text field |

**Fail:** GET=`0`/`≠1` with only `0x90`.  
**Fail:** GET=`3` but `0x0c` silent because `0xAF TOUCH_PEN` / payload-B3 draw routing ran afterward.  
**Fail:** Using soft multi-touch on `0x90` as “replication.”

### Debug order when stuck

1. Log GET scenario **before and after** every leave / detach / reassert.
2. Confirm which hidraw has `0x0c` in the descriptor **and** whether `read()` returns any `0x0c` reports.
3. Temporarily **disable** `touch_userspace_config` / `ite8951_reapply_draw_input` / `reassert_draw_scenario` after a manual MT entry and retest `0x0c`.
4. Diff kernel CDB for `0xA6`/`0xB3` against `E-usbc-ops.txt` (address `0x01030100`, B3 arg triples).

---

## Open (Windows→Linux)

- Minimal cold subset that latches GET=`3` + live `0x0c` **without** Homebar (may need more than `mt-replay`).
- Meaning of `0xAE`; exact `0xB3` CDB field map.
- Clean re-enter keyboard from GET=`3`.

---

## Do not

- Re-validate bare `0x03000000` leave-KB as the MT solution.
- Treat GET=`0` as pen-mouse MT success.
- Route MT mode through `SET_TP_AREA` `TOUCH_PEN=0x03` “to enable HID.”
- Spend time on soft `0x90` clustering instead of live `0x0c`.
