# Lenovo YogaBook C930 E-Ink Linux — Architecture Reference

## 1. Architectural intent

Implement the E-Ink subsystem as a standard Linux display stack with explicit
layer separation:

| Layer | Purpose |
|-------|---------|
| Kernel DRM driver | Hardware ownership, panel transport, update execution |
| Userspace mode daemon | Policy, mode changes, input orchestration |
| Applications | Standard Linux graphics and input consumers |

## 2. Production boundary

| Area | Production status |
|------|-------------------|
| `kernel/` | Production implementation area |
| `userspace/` | Production implementation area |
| `docs/` | Specification and reverse-engineering record |
| `reference/` | Read-only research input only |
| `scripts/` | Validation and capture tooling, not product runtime |

## 3. Ownership map

| Concern | Owning layer | Excluded layers |
|---------|--------------|-----------------|
| USB framing, register I/O, RAM load, blit | Kernel protocol layer | Userspace |
| DRM mode, connector, framebuffer commit | Kernel DRM layer | Userspace |
| Input mode selection and policy | Userspace daemon | Kernel DRM core |
| Touch parsing to desktop-visible input | Userspace | Kernel display path |
| Hardware behavior documentation | Docs and reference archives | Runtime code |

## 4. Repository structure

| Path | Role |
|------|------|
| `kernel/eink_drm/` | Active clean-room Linux driver |
| `kernel/eink_drm/protocol/` | Low-level ITE8951 transport and opcodes |
| `kernel/eink_drm/drm/` | DRM/KMS binding, connector, atomic update path |
| `userspace/` | Planned daemon and helper tools |
| `scripts/windows/` | Windows trace acquisition and WinUSB experiments |
| `win-captures/` | Capture outputs and analysis evidence |

## 5. Kernel internal split

| Subarea | Responsibility |
|---------|----------------|
| Probe / lifecycle | USB bind, detach, autosuspend policy, state reset |
| Protocol | USBC/USBS framing, command composition, register accesses |
| Mode transition | Keyboard exit, draw-mode entry, panel-ready polling |
| Pixel path | Load image data into panel RAM and trigger display update |
| DRM interface | Expose connector, import framebuffer, emit updates |

## 6. Userspace target split

| Component | Responsibility | Status |
|-----------|----------------|--------|
| `einkd` | D-Bus API, mode policy, sleep/resume coordination | Planned |
| Keyboard emulator | Layout rendering reference, touch-to-key mapping, uinput | Planned |
| `eink-touchpad` | Touch-to-pointer / gesture proof-of-concept | Experimental |
| Reader / draw plugins | Mode-specific tooling | Planned |

## 7. Control flow model

### 7.1 Display path

| Step | Action |
|------|--------|
| 1 | Linux compositor or app renders into framebuffer |
| 2 | DRM update hook imports framebuffer contents |
| 3 | Kernel converts or uploads panel byte stream |
| 4 | Kernel issues LD_IMG to panel RAM |
| 5 | Kernel issues BLIT with waveform and rectangle |
| 6 | Panel refresh occurs on E-Ink display |

### 7.2 Input / mode path

| Step | Action |
|------|--------|
| 1 | Userspace requests keyboard, draw, display, or MT mode |
| 2 | Userspace coordinates with kernel-owned display state |
| 3 | Kernel or companion helper issues scenario/register changes |
| 4 | HID report class changes according to firmware mode |
| 5 | Userspace claims and translates touch events as required |

## 8. Architectural rules

| Rule | Consequence |
|------|-------------|
| No production dependence on Lenovo services | Linux runtime must stand alone |
| No copy-forward from reference source trees | Behavior may be replicated, code may not |
| No `/dev/eink0` command channel as target design | Standard DRM and uinput interfaces are required |
| Kernel retains pixel ownership | Userspace tools do not become alternate blitters |
| Reference docs do not override device validation | On-device behavior is authoritative |

## 9. Reference archive usage policy

| Reference source | Allowed use | Prohibited use |
|------------------|------------|----------------|
| 2019 Linux driver | Learn opcode order, memory addressing, legacy behavior | Reuse module structure or code blocks |
| Lenovo Windows source tree | Learn structs, command tables, HID semantics | Port UI, services, DLL logic, or implementation bodies |
| Packet captures | Verify command sequences and state transitions | Treat as substitute for Linux validation |

## 10. Data contracts

| Contract | Medium |
|----------|--------|
| Display buffer submission | DRM framebuffer commit |
| Mode control | Planned D-Bus `org.yogabook.Eink` API |
| Input publication | `uinput` virtual devices |
| Hardware protocol constants | Kernel headers and docs |

## 11. E-Ink usage constraints

| Constraint | Architectural implication |
|------------|--------------------------|
| Slow refresh and ghosting | Refresh policy cannot mirror LCD assumptions |
| Multiple waveform classes | Update pipeline must be mode-aware |
| Firmware keyboard mode conflicts with Linux input ownership | Explicit scenario management is required |
| MT enable sequence affects display config | Input and display state are not independent |

## 12. Known architectural incompletions

| Area | Missing element |
|------|-----------------|
| Refresh engine | Dirty-region diff, waveform heuristics, cleanse budget |
| Input daemon | Final mode-state owner and D-Bus API |
| Reliability | Suspend/resume, boot packaging, stable connector identity |
| MT support | Linux-native reproduction of validated Windows arm sequence |

## 13. Architectural end state

| Property | Definition |
|----------|------------|
| Display identity | E-Ink panel appears as a regular DRM output |
| Input identity | Touch is exposed via standard Linux input devices |
| Policy placement | Mode and user workflow logic live in userspace |
| Reference discipline | Reverse-engineering artifacts remain external evidence only |

## 14. Archive note

This architecture file is a stable description of intended and partially
implemented boundaries. It is suitable for a future restart without requiring
reconstruction of the repository's design intent from narrative notes.
