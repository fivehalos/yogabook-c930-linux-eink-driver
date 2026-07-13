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
| GET | `0x00000000` | 4 B; **byte1** = live scenario | `1` = firmware keyboard; `0` = non-keyboard / draw path |
| SET keyboard | `0x01000000` (`1 << 24`) | 4 B (often echoes) | Seen in `C-penmouse.pcap` Homebar KB path |
| Leave keyboard | `0x03000000` (`3 << 24`) | then GET → **byte1=`0`** | **Validated** with EinkSvr stopped: stops Notepad phantom typing |
| KB arm (legacy) | `0x01010000` | echo | Seen in C; not required for leave-KB |

Notes:

- Do **not** use low DWORD `address = 1` or `address = 3` as the primary encoding.
- Success for “stop typing” is **GET ≠ `1`**, usually **`0`**. Firmware does **not**
  GET-report `3` after a working leave-KB.
- `0x03000000` was **not** observed in Homebar `C` traffic (EinkSvr uses more
  `0xB3`/`0xAF`); the WinUSB tool proved `0x03000000` alone is sufficient to exit KB.
- Re-enter keyboard from `0` via `0x01000000` alone did **not** latch in tool tests —
  open.

---

## Minimal leave-keyboard sequence (tool-proven)

EinkSvr stopped; open WinUSB MI_00:

1. Optional: `0xB3` CDB-only (no OUT payload) args  
   `arg1=0x0100`, `arg2=0x0103`, `arg3=0x0301`, `arg4=0`  
   (from `C-penmouse.pcap`; may be optional)
2. Optional: `0xA9` SET_WAVEFORM arg1=`0x0200`
3. **`0xA6` SET address `0x03000000`** (expect 4 B IN + USBS)
4. **`0xA6` GET** → expect byte1=`0`

Replay: `scripts/windows/eink-winusb/EinkWinUsb.exe pen-mouse`

---

## Related opcodes (seen; not fully specified here)

| Op | Role in C / S |
|----|----------------|
| `0xB3` | Dynamic flags — Windows often packs values **in CDB args**, no payload |
| `0xAF` | TP areas — Windows often packs geometry **in CDB**; IN echo ~10 B |
| `0xAC` | Handwriting region clear (`addr=0`) |
| `0xA9` | Waveform (`0x0200` on mode transitions) |
| `0x83`/`0x84` | Read/write regs (`0x18001224` panel mode, `0x18001138` display_cfg) |
| `0x80` | GET_SYS doorknock |

Full Homebar blit / cold enable (`S-einksvr-restart.pcap`) is separate from
leave-KB.

---

## Open questions

- Reliable **re-enter keyboard** from scenario `0` without EinkSvr
- Exact `0xB3` / `0xAF` field map vs Lenovo structs
- Whether Linux must also send B3/AF after `0x03000000`, or A6 alone is enough
- Port: use `(u32)scenario << 24` in `ite8951_try_set_coordinator_scenario()`;
  treat GET=`0` after requesting pen-mouse as success
