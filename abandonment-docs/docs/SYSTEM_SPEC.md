# Lenovo YogaBook C930 E-Ink Linux — System Specification

## 1. System objective

Provide Linux control of the Lenovo YogaBook C930 E-Ink panel as a native
display-and-input subsystem, replacing reliance on Lenovo's Windows runtime and
retiring the original 2019 proof-of-concept driver architecture.

## 2. Hardware target

| Parameter | Value |
|-----------|-------|
| Device family | Lenovo YogaBook C930 |
| Product variant referenced in docs | YB-J912 |
| Controlled component | E-Ink keyboard/pad/display deck |
| Main controller identity | USB composite device `048d:8951` |
| Display resolution | 1920 x 1080 |
| Display controller class | ITE8951-compatible vendor protocol |

## 3. Functional scope

| Function | Required behavior |
|----------|-------------------|
| Display output | Present E-Ink as a Linux DRM/KMS connector |
| Image transport | Upload pixel data through vendor bulk protocol |
| Update execution | Issue panel blit/update commands with waveform selection |
| Input mode control | Exit firmware keyboard mode; enter draw or MT-capable modes |
| Touch interpretation | Translate HID reports into Linux-consumable input events |
| Keyboard replacement | Support post-boot virtual keyboard layouts via userspace |

## 4. Excluded scope

| Exclusion | Rationale |
|-----------|-----------|
| Shipping Lenovo Windows binaries or UI | Unsupported and contrary to project direction |
| Porting reference driver source into production | Clean-room requirement |
| Maintaining `/dev/eink0` text-command model as target architecture | Replaced by DRM/KMS and standard interfaces |
| Building a full graphical Lenovo-style E-Ink shell | Not necessary for Linux handoff goals |

## 5. Software decomposition

| Layer | Responsibility | Status |
|-------|----------------|--------|
| `kernel/eink_drm` | USB transport, panel control, DRM connector, framebuffer commit path | Active |
| `userspace/einkd` | Planned mode control daemon and uinput orchestration | Planned |
| `userspace/eink-touchpad` | Early touch-to-uinput prototype | Experimental |
| `scripts/windows` | Windows-side capture and protocol validation tools | Present |
| `reference/` | Research-only material | External to production |

## 6. USB subsystem model

| Subsystem | Transport | Function |
|-----------|-----------|----------|
| Display engine | Vendor bulk interface | Commands, register access, RAM upload, blit |
| Touch / pen | HID | Contact reports |
| Firmware keyboard | HID keyboard | Touch-to-key translation in Lenovo keyboard mode |

### 6.1 Vendor bulk exchange format

| Stage | Operation |
|-------|-----------|
| 1 | 31-byte USBC control packet on bulk OUT |
| 2 | Optional data IN or OUT |
| 3 | 13-byte USBS status drain on bulk IN |

### 6.2 Core operational assumptions

| Assumption | Description |
|------------|-------------|
| Persistent IN path required | Bulk input is reused and drained after each command |
| Async OUT required | Synchronous `usb_bulk_msg()` is known-bad on this panel |
| Addressing is fixed for image transport | Panel RAM transfer base is not taken from GET_SYS image buffer |

## 7. Display specification

| Item | Value / behavior |
|------|------------------|
| Mode exposed to Linux | 1920 x 1080 connector mode |
| Driver model | `drm_simple_display_pipe` based atomic path |
| Pixel representation | 1 byte per pixel in current prototype path |
| Transfer chunk ceiling | 61440 bytes (`1920 x 32`) |
| Base transfer address | `0x00382f30` |
| Current validated waveform payload value | `0xff` |

## 8. Control and register points

| Item | Identifier | Role |
|------|------------|------|
| Scenario opcode | `0xA6` | Query or change firmware operating scenario |
| Draw upload opcode | `0xA8` | Load image data into panel memory |
| Display blit opcode | `0x94` | Execute panel update from memory |
| Display config register | `0x18001138` | Draw/MT configuration |
| Mode status register | `0x18001224` | Transition / ready-state observation |
| Dynamic flags opcode | `0xB3` | Auxiliary mode control in Windows traces |

## 9. Scenario-state model

| State indicator | Meaning | Observed effect |
|-----------------|---------|-----------------|
| GET byte1 = `1` | Firmware keyboard active | Touch can type into host apps |
| GET byte1 = `0` | Non-keyboard bare leave state | Typing stopped; Linux currently sees single-contact path |
| GET byte1 = `3` | MT-oriented scenario state | Required precursor to finger MT latch |

## 10. Multitouch specification

### 10.1 Target behavior

| Requirement | Expected outcome |
|-------------|------------------|
| Firmware keyboard disabled | No phantom typing on LCD session |
| HID MT class active | Report stream `0x0c` with contact count |
| Display ownership retained by Linux | DRM blits remain functional after MT arm |
| Userspace integration | HID contacts become uinput pointer / gesture actions |

### 10.2 Windows-validated arm conditions

| Condition | Value |
|-----------|-------|
| Scenario after MT entry | GET byte1 = `3` |
| Finger-enable effect | `display_cfg |= 0x00080000` |
| Example post-arm register value | `0x00280000` |
| Success HID report class | `0x0c` |

### 10.3 Non-success conditions

| Condition | Result |
|-----------|--------|
| Bare `0x03000000` leave only | Single-contact class, not true MT |
| Scenario `3` without finger-enable bit | Pen-only or no verified `0x0c` stream |
| Running touch-pen draw routing after MT arm | Risks collapsing back to non-MT input path |

## 11. Userspace specification

| Component | Required behavior |
|-----------|-------------------|
| `einkd` | Mode state machine, D-Bus API, sleep/resume coordination |
| Keyboard emulator | Layout image + touch mapping + uinput key generation |
| Touchpad path | Contact parsing, gesture mapping, pointer synthesis |
| Reader/draw plugins | Optional future consumers of the display and input stack |

## 12. Validation criteria

| Domain | Pass condition |
|--------|----------------|
| Display probe | Driver binds and initializes USB panel path |
| Frame output | Visible panel update from DRM framebuffer commit |
| Scenario exit | GET state is not `1` after Linux handoff |
| Bare leave path | Phantom typing stops |
| MT path | `0x0c` reports show contact count >= 2 |
| Display coexistence | Linux blit path remains functional after MT arm |

## 13. Major known gaps

| Gap | Impact |
|-----|--------|
| Linux implementation of MT arm sequence incomplete | Prevents verified multitouch on Linux |
| Refresh policy incomplete | Daily usability and ghosting control not production-ready |
| Suspend/resume sequence incomplete | Reliability gap for real mobile usage |
| Userspace daemon unimplemented | No production mode orchestration or custom keyboard stack |

## 14. Archive interpretation

This specification describes the highest-confidence technical state derivable
from the repository and the Windows validation notes. It is suitable for
restart, forensic review, or migration planning. It is not a declaration of
feature completeness.
