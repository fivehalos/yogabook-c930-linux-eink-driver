# Windows-validated ITE8951 coordinator protocol

Concise wire notes from YogaBook C930 USB captures (`win-captures/`) and the
WinUSB tool (`scripts/windows/eink-winusb/`). Framing matches the clean-room
kernel path; **scenario encoding below supersedes older “address = 1/3” guesses.**

Capture how-to: [WINDOWS_CAPTURE.md](WINDOWS_CAPTURE.md).  
Pixel xfer/blit: [BLUEPRINT.md](BLUEPRINT.md) (unchanged).

---

## Bulk transport (BOT)

MI_00 vendor bulk: **OUT `0x02`**, **IN `0x81`** (512-byte MPS).

Every command:

1. **USBC** control OUT — 31 bytes (`"USBC"` + length/flags + SCSI `0xFE` CDB)
2. Optional data OUT/IN (payload or response)
3. **USBS** status IN — 13 bytes (`"USBS"`…)

**Must drain USBS after every exchange.** Skipping it wedges the next OUT
(Windows: `wait failed: 258` / error 121) until IN drain or reboot.

Response + status may arrive as two short IN transfers; treat them separately.

---

## Control packet (31 B)

| Offset | Field |
|--------|--------|
| 0–7 | `"USBC"` + tag bytes (`61 89 51 89` on this panel) |
| 8–11 | LE response length (0 if no data-IN) |
| 12 | `0x80` = expect IN data; `0x00` = none |
| 14 | CDB length `0x10` |
| 15 | SCSI `0xFE` |
| 16 | LUN `0` |
| 17–20 | **Address** BE32 |
| 21 | ITE opcode |
| 22–29 | arg1…arg4 BE16 |
| 30 | `0` |

---

## Scenario `0xA6` (validated)

| Op | Address | Response | Meaning |
|----|---------|----------|---------|
| GET | `0x00000000` | 4 B; **byte1** = live scenario | `1` = firmware keyboard; `0` = non-KB / draw (bare leave); **`3` = pen-mouse / MT latch** |
| SET keyboard | `0x01000000` (`1 << 24`) | 4 B (often echoes) | Seen in `C-penmouse.pcap` Homebar KB path |
| Leave keyboard (bare) | `0x03000000` (`3 << 24`) | then GET → **byte1=`0`** | Stops Notepad typing; **single-contact** HID `0x90` class on Linux |
| KB arm (legacy) | `0x01010000` | echo | Seen in C; not required for leave-KB |

Notes:

- Do **not** use low DWORD `address = 1` or `address = 3` as the primary encoding.
- Two different non-keyboard outcomes after leave:
  - **GET=`0`**: WinUSB `pen-mouse` / bare `0x03000000` — enough to stop typing; Linux still only gets single-point `0x90`.
  - **GET=`3`**: MT leave path (`0xA6` `0x01030100`…); finger multitouch needs **`display_cfg |= 0x00080000`** → HID `0x0c` on EP `0x85`.
- `0x03000000` alone was **not** enough for MT; cold leave uses `0xA6` **`0x01030100`** plus `0xB3`/`0xA9`/`0xAE`/`0xAC`.
- Re-enter keyboard from `0` via `0x01000000` alone did **not** latch in tool tests —
  open. Same open for exit from GET=`3` (use brief EinkSvr bounce for KB retest).

---

## Minimal leave-keyboard sequence (tool-proven — stops typing only)

EinkSvr stopped; open WinUSB MI_00:

1. Optional: `0xB3` CDB-only (no OUT payload) args  
   `arg1=0x0100`, `arg2=0x0103`, `arg3=0x0301`, `arg4=0`  
   (from `C-penmouse.pcap`; may be optional)
2. Optional: `0xA9` SET_WAVEFORM arg1=`0x0200`
3. **`0xA6` SET address `0x03000000`** (expect 4 B IN + USBS)
4. **`0xA6` GET** → expect byte1=`0`

Replay: `scripts/windows/eink-winusb/EinkWinUsb.exe pen-mouse`

---

## Multitouch latch + owner blit (validated Jul 2026)

**Cold armed path (preferred — no Homebar):**

1. EinkSvr stopped/disabled; `EinkWinUsb.exe mt-arm`  
   (`mt-enter` = E ops 1–20 → GET=`3`, then finger-enable).
2. Finger-enable: `0xB3` `0100/0003/0301` + **`0x84` `0x18001138` OR `0x00080000`**  
   (wire `00-20-00-00` → `00-28-00-00`).
3. Expect GET=`3`, cfg=`0x00280000`, no LCD typing, finger MT / HID `0x0c`.
4. Owner `fill` / `stripes` at `0x00382f30` can replace KB ghost art.

**Homebar path (also works):** pen/touchpad + **top-left finger-enable** → same latch; then stop EinkSvr and take MI_00.

**Incomplete alone:**

- `mt-enter` without finger bit → GET=`3`, KB off, **pen-only** (often `0x90`, no `0x0c`).
- Pen-only Homebar (no finger-enable) → same miss.

**Linux handoff:** [LINUX_MT_STEPS.md](LINUX_MT_STEPS.md) — port `mt-arm`; do not run `TOUCH_PEN` after arm.

---

## Related opcodes (seen; not fully specified here)

| Op | Role in C / S / E |
|----|----------------|
| `0xB3` | Dynamic flags — Windows often packs values **in CDB args**, no payload |
| `0xAF` | TP areas — Windows often packs geometry **in CDB**; IN echo ~10 B |
| `0xAE` | Present in E MT entry (`arg1=0x0100`) — open |
| `0xAC` | Handwriting region clear (`addr=0`) |
| `0xA9` | Waveform (`0x0200` on mode transitions) |
| `0x83`/`0x84` | Read/write regs — **`0x18001138` display_cfg**; finger MT bit **`0x00080000`** |
| `0x80` | GET_SYS doorknock |
| `0xA8`/`0x94` | LD_IMG / DPY blit — owner path works after Homebar-armed GET=`3` |

Full Homebar blit / cold enable (`S-einksvr-restart.pcap`) is KB-class bring-up
(GET=`1`); not the finger-enable delta.

---

## Open questions

- Reliable **re-enter keyboard** from scenario `0` or `3` without EinkSvr
- Exact `0xB3` / `0xAE` field map vs Lenovo structs (finger path uses CDB `0100/0003/0301`)
- Linux: confirm `0x0c`→uinput + DRM blit after ported `mt-arm`
