# Windows USB Capture Procedure — Technical Reference

## 1. Purpose

Use Windows on the YogaBook C930 as an observation environment for Lenovo's
coordinator behavior. Capture vendor USB traffic and HID transitions needed to
reconstruct Linux mode control, especially keyboard exit and multitouch entry.

## 2. Usage model

| Statement | Meaning |
|-----------|---------|
| Windows is not the target runtime | Captures are evidence, not deployment artifacts |
| VM capture is not acceptable | The internal USB composite device must be observed on bare metal |
| Linux replay is the target | All useful output is a sequence, register, or HID interpretation |

## 3. Required environment

| Requirement | Value |
|-------------|-------|
| Device | YogaBook C930 hardware |
| OS | Native Windows installation on the device |
| USB capture stack | Wireshark + USBPcap |
| Lenovo runtime | E-Ink services present for OEM behavior capture |
| Output location | `win-captures/` or equivalent archive directory |

## 4. One-time setup

| Step | Action |
|------|--------|
| 1 | Install Windows on the YogaBook C930 |
| 2 | Disable Fast Startup |
| 3 | Install Lenovo E-Ink software stack |
| 4 | Install Wireshark and USBPcap |
| 5 | Load protocol dissector if available |
| 6 | Confirm device `048d:8951` is present and Lenovo services run |

## 5. Capture objective matrix

| Capture | Primary question answered |
|---------|---------------------------|
| Cold boot / service restart | What initialization traffic arms Lenovo's default state |
| Keyboard mode | What traffic correlates with firmware keyboard behavior |
| Pen/touchpad leave path | What traffic exits keyboard mode |
| MT path | What additional traffic enables `0x0c` multitouch reports |
| Round-trip mode switches | What state must be restored or preserved |

## 6. Priority captures

| Priority | File label | Purpose |
|----------|------------|---------|
| 1 | `C-penmouse` or equivalent | Compare keyboard vs non-keyboard transition |
| 2 | `S-einksvr-restart` | Observe service-driven initialization |
| 3 | MT-focused sequence | Observe `GET=3` transition and finger-enable delta |
| 4 | Cold boot idle trace | Identify earliest boot-time state configuration |

## 7. Standard capture procedure

| Step | Action |
|------|--------|
| 1 | Start USBPcap capture for the bus owning `048d:8951` |
| 2 | Wait several seconds before interaction |
| 3 | Perform exactly one target action or short action sequence |
| 4 | Wait several seconds after action completion |
| 5 | Stop capture |
| 6 | Record file name, time, and user action in notes |

## 8. Required comparison targets

| Comparison | Goal |
|------------|------|
| Keyboard mode vs pen/touchpad mode | Isolate leave-keyboard traffic |
| Scenario `0` path vs scenario `3` path | Separate bare leave from MT-capable entry |
| MT arm before vs after finger-enable | Identify minimal MT latch delta |
| Service running vs service stopped | Determine which state persists without Lenovo ownership |

## 9. Known command features to inspect

| Item | Identifier | Reason |
|------|------------|-------|
| Scenario query/set | `0xA6` | Determine keyboard, bare leave, and MT states |
| Dynamic flags | `0xB3` | Present in both scenario and MT sequences |
| Waveform-related mode step | `0xA9` | Seen in mode changes |
| Draw or helper reset | `0xAC`, `0xAE` | Present in MT entry path |
| Register write | `0x84` | Used to set `display_cfg` MT bit |
| Display config register | `0x18001138` | MT finger-enable latch |
| Owner blit commands | `0xA8`, `0x94` | Verify display handoff feasibility |

## 10. Expected scenario interpretations

| Observation | Meaning |
|-------------|---------|
| `0xA6` GET byte1 = `1` | Firmware keyboard active |
| `0xA6` GET byte1 = `0` after leave | Typing suppressed only |
| `0xA6` GET byte1 = `3` | MT-oriented scenario state entered |
| `display_cfg` OR `0x00080000` | Finger-enable applied |

## 11. MT-specific capture requirements

| Requirement | Reason |
|-------------|--------|
| Include action that actually enables finger MT | Pen-only paths are insufficient |
| Preserve HID evidence | `0x0c` must be seen, not inferred |
| Note whether Lenovo UI remains running | Helps separate service dependency from mode persistence |
| Test post-arm service stop if possible | Confirms Linux can take ownership after arm |

## 12. Success criteria for a useful capture

| Criterion | Description |
|-----------|-------------|
| Sequence isolation | Minimal unrelated traffic around target action |
| Time annotation | Enough metadata to map action to trace position |
| Scenario evidence | `0xA6` transitions are observable |
| HID evidence | `0x0c`, `0x90`, or keyboard behavior differences are observable |
| Reproducibility | Action can be repeated to confirm interpretation |

## 13. Post-capture extraction targets

| Output | Use in Linux work |
|--------|-------------------|
| Opcode order | Implement or verify Linux command sequence |
| Register values | Confirm fixed constants or mode bits |
| HID report IDs and structure | Select correct Linux parser path |
| State persistence notes | Decide whether Lenovo service must remain active during arm |

## 14. Known practical constraints

| Constraint | Effect |
|------------|--------|
| USBPcap interface discovery can be unreliable | Enumeration must be verified explicitly |
| Fast Startup corrupts clean reboot assumptions | Must be disabled |
| Lenovo software absence changes baseline behavior | Captures must declare service state |
| VM passthrough is insufficient | Native dual-boot remains required |

## 15. Deliverables from a complete Windows session

| Deliverable | Minimum content |
|-------------|-----------------|
| Capture files | Keyboard, leave-keyboard, MT-arm, and init traces |
| Notes | Timestamped action log |
| Scenario table | Observed GET values before and after each action |
| HID summary | Which report IDs appeared in each mode |
| Register summary | Any observed writes to `0x18001138` and related registers |

## 16. Archive note

This document defines what a future investigator must collect from Windows to
resume Linux protocol work without reconstructing the capture methodology from
long-form prose.
