# Linux Multitouch Enablement — Technical Procedure

## 1. Objective

Reproduce on Linux the Windows-validated path that:

1. Disables firmware keyboard typing.
2. Preserves Linux display ownership of the E-Ink panel.
3. Enables true finger multitouch HID reporting.
4. Feeds multitouch into a userspace uinput translation path.

## 2. Success definition

| Check | Required result |
|-------|-----------------|
| Scenario state | `0xA6` GET byte1 is not `1` |
| LCD side effect | No phantom typing into host applications |
| HID class | Report stream `0x0c` present |
| Contact count | `byte2 >= 2` during multi-finger contact |
| Display continuity | Linux LD_IMG / BLIT path still works after arm |

## 3. Non-success states

| State | Interpretation |
|-------|----------------|
| `0xA6` GET byte1 = `1` | Firmware keyboard still active |
| `0xA6` GET byte1 = `0` with only HID `0x90` | Bare leave state; not true multitouch |
| `0xA6` GET byte1 = `3` without `0x0c` reports | MT entry incomplete |
| Display becomes Lenovo-owned or unusable | Handoff failed |

## 4. Windows-derived state model

| Mode | GET byte1 | HID outcome | Operational meaning |
|------|-----------|-------------|---------------------|
| Firmware keyboard | `1` | Keyboard behavior | Lenovo typing mode |
| Bare leave | `0` | Usually `0x90` single-contact | Typing suppressed only |
| MT-oriented scenario | `3` | `0x0c` only after finger-enable | Required precondition for finger MT |

## 5. Required implementation targets

| Location | Required work |
|----------|---------------|
| Kernel protocol path | Expose or implement MT entry sequence and register writes |
| Touch reader | Select MT-capable HID endpoint rather than single-contact path |
| Userspace input bridge | Parse `0x0c` contacts and publish uinput events |
| Mode control | Prevent conflicting draw-path touch mode after MT arm |

## 6. Minimal validated facts

| Fact | Value |
|------|-------|
| Bare leave-keyboard command | `0xA6` SET addr `0x03000000` |
| MT scenario transition indicator | `0xA6` GET byte1 = `3` |
| Finger-enable register bit | `0x00080000` in `display_cfg` |
| Display config register | `0x18001138` |
| Successful post-arm config example | `0x00280000` |
| MT HID report identifier | `0x0c` |

## 7. Cold-path procedure to port

### 7.1 Entry sequence requirement

The Linux path must reproduce the Windows `mt-arm` sequence, not only the bare
leave-keyboard step.

### 7.2 Sequence phases

| Phase | Purpose |
|-------|---------|
| Scenario leave / entry | Transition away from firmware keyboard |
| Dynamic mode preparation | Reproduce `0xB3`, `0xA9`, `0xAE`, `0xAC` side effects seen in Windows |
| Scenario verification | Confirm GET byte1 becomes `3` |
| Finger-enable | Set MT-specific configuration bit |
| HID verification | Confirm `0x0c` stream with multi-contact reports |

## 8. Validated scenario-half sequence

The following sequence is the known scenario-entry half that results in GET
byte1 = `3` in Windows experiments:

| Step | Opcode | Address | Arg1 | Arg2 | Arg3 | Notes |
|------|--------|---------|------|------|------|-------|
| 1 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | Read helper state |
| 2 | `0xB3` | 0 | `0x0101` | `0x0003` | `0x0301` | CDB-inline |
| 3 | `0xA9` | 0 | `0x0200` | 0 | 0 | Waveform-related mode step |
| 4 | `0xA6` | `0x01030100` | 0 | 0 | 0 | Scenario set |
| 5-7 | `0x83` / `0x84` | panel / cfg regs | - | - | - | Register handling |
| 8 | `0xAE` | 0 | `0x0100` | 0 | 0 | Auxiliary mode step |
| 9 | `0xAC` | 0 | 0 | 0 | 0 | Clear or reset helper state |
| 10 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | Read helper state |
| 11 | `0xB3` | 0 | `0x0100` | `0x0003` | `0x0301` | CDB-inline |
| 12 | `0xA9` | 0 | `0x0200` | 0 | 0 | Repeat mode setup |
| 13 | `0xA6` | `0x01030100` | 0 | 0 | 0 | Scenario set |
| 14 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | Read helper state |
| 15 | `0xB3` | 0 | `0x0100` | `0x0003` | `0x0201` | Variant |
| 16 | `0x80` | `0x38393531` | 1 | 2 | 0 | GET_SYS / doorknock form |
| 17-19 | Register operations | - | - | - | - | Panel state setup |
| 20 | `0xA6` GET | `0` | - | - | - | Expect byte1 = `3` |

## 9. Finger-enable requirement

Scenario `3` alone is not sufficient. The MT path requires an additional
finger-enable operation:

| Step | Action |
|------|--------|
| 1 | Read state using `0x81` at `0x80`, length 16 |
| 2 | Issue `0xB3` with args `0x0100 / 0x0003 / 0x0301` |
| 3 | Read `display_cfg` from `0x18001138` |
| 4 | Write `display_cfg | 0x00080000` back to `0x18001138` |
| 5 | Confirm resulting config retains expected MT bit |

## 10. HID format requirement

The MT reader must target the `0x0c` report class:

| Field | Meaning |
|-------|---------|
| byte0 | Report ID `0x0c` |
| byte1 | Padding / flags area |
| byte2 | Contact count |
| remaining data | Repeated contact structures |

Contact structure, per observed notes:

| Offset in contact block | Meaning |
|-------------------------|---------|
| 0 | Tip / flags |
| 1 | Contact ID |
| 2 | Padding |
| 3-4 | X coordinate, little-endian |
| 5-6 | Y coordinate, little-endian |

## 11. Linux integration sequence

| Order | Action |
|-------|--------|
| 1 | Ensure firmware keyboard leave or MT entry logic is available in Linux |
| 2 | Do not run legacy `TOUCH_PEN` routing after MT arm |
| 3 | Arm MT sequence |
| 4 | Verify `0xA6` GET byte1 = `3` |
| 5 | Verify `display_cfg` contains `0x00080000` |
| 6 | Open correct HID/hidraw interface and confirm `0x0c` reports |
| 7 | Map contacts to uinput device semantics |
| 8 | Reconfirm Linux display blits remain operational |

## 12. Validation checklist

| Category | Check |
|----------|-------|
| Scenario | GET transitions from `1` to `3` |
| Keyboard suppression | Notepad-style phantom typing is absent |
| HID | `0x0c` packets observed with 2+ contacts |
| Display | Owner fill/stripes or DRM blit still works |
| Userspace | uinput device emits expected gesture or pointer events |

## 13. Common failure interpretations

| Symptom | Likely cause |
|---------|--------------|
| GET becomes `0`, never `3` | Only bare leave path was implemented |
| GET is `3`, no `0x0c` | Finger-enable bit missing or wrong HID interface selected |
| `0x0c` briefly appears then disappears | Later mode operation overwrote MT state |
| MT works but display path breaks | Display ownership or register state was not preserved |

## 14. Implementation priority

| Priority | Work item |
|----------|-----------|
| 1 | Port `mt-arm` sequence semantics into Linux-accessible code |
| 2 | Ensure correct HID interface discovery for `0x0c` |
| 3 | Complete `0x0c` contact parser and uinput publisher |
| 4 | Add mode coordination so display and MT paths coexist |

## 15. Archive note

This file is a procedural specification, not a historical narrative. It
captures the Linux-side actions required to reproduce the only validated true
multitouch path described in the repository materials.
