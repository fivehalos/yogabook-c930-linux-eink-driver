# Lenovo YogaBook C930 E-Ink Linux — Repository Technical Summary

## Purpose

This package is a reference-style extraction of the repository's current
technical state. It is intended for abandonment, handoff, or archival use.
It describes the implemented subsystems, validated behavior, known gaps, and
operational constraints of the Linux effort for the Lenovo YogaBook C930 E-Ink
panel.

## Scope

| Area | Covered |
|------|---------|
| Hardware target | Lenovo YogaBook C930 (YB-J912) E-Ink panel subsystem |
| Kernel work | Clean-room USB + DRM/KMS display driver |
| Userspace work | Planned mode daemon; early touchpad prototype |
| Reverse engineering | Windows USB capture and scenario transition notes |
| Archive policy | Reference material is documentation input only |

## System identity

| Item | Value |
|------|-------|
| Product | Lenovo YogaBook C930 |
| Target subsystem | Secondary E-Ink panel / keyboard deck |
| Primary USB device | `048d:8951` |
| Panel resolution | 1920 x 1080 |
| Production implementation language | C |
| Kernel architecture | Linux DRM/KMS over vendor USB transport |
| Userspace architecture | Plain POSIX/systemd-oriented C components |

## Repository status

| Component | State |
|-----------|-------|
| `kernel/eink_drm/` | Active implementation |
| `userspace/` | Skeleton / prototype stage |
| `docs/` | Design, protocol, reverse-engineering notes |
| `reference/` | Read-only research material; not production code |
| `scripts/` | Bring-up, capture, and hardware validation helpers |

## Implemented capabilities

| Capability | Status | Notes |
|-----------|--------|-------|
| USB probe of `048d:8951` vendor interface | Implemented | Clean-room kernel path |
| ITE8951 doorknock / GET_SYS sequence | Implemented | Validated on hardware |
| Draw-mode transition for display ownership | Implemented | Enables panel blit path |
| LD_IMG / BLIT upload path | Implemented | Uses fixed xfer base |
| DRM connector registration | Implemented | 1920 x 1080 output path |
| Atomic framebuffer commit path | Implemented | `drm_simple_display_pipe` based |
| Live compositor output on E-Ink | Validated | Reported with niri |

## Partially implemented or pending capabilities

| Capability | State | Blocking details |
|-----------|-------|------------------|
| Smart partial refresh policy | Not complete | Dirty-region diff and waveform policy incomplete |
| Ghosting budget / scheduled cleanse | Not complete | Requires refresh accounting |
| Stable boot-time packaging | Not complete | udev, module autoload, naming not finalized |
| Suspend / resume recovery | Not complete | Re-init sequence not fully defined |
| Multitouch handoff on Linux | Not complete | Windows flow understood; Linux port pending |
| Firmware-keyboard replacement | Not complete | Userspace daemon and layout engine not finished |

## Current subsystem model

| Subsystem | Interface class | Function |
|-----------|-----------------|----------|
| ITE8951 display controller | Vendor bulk | Panel init, memory load, blit, waveform control |
| Touch controller | HID | Finger / stylus reports |
| Firmware keyboard | HID keyboard | Pre-OS and Lenovo-managed keyboard mode |

## Design rules

| Rule | Meaning |
|------|---------|
| Clean-room production code only | Reference archives inform behavior but are not copied |
| DRM owns pixels | Userspace must not perform production blits |
| Userspace owns policy and mode changes | Input mode selection and keyboard emulation belong outside DRM |
| Text-first E-Ink workflow | Refresh policy targets low-motion, low-ghosting interfaces |
| Windows is a measurement environment | Windows traces are used to derive Linux behavior, not to preserve Lenovo software |

## Critical validated constants

| Item | Value | Use |
|------|-------|-----|
| Transfer / blit base address | `0x00382f30` | Panel RAM base for LD_IMG and BLIT |
| Display configuration register | `0x18001138` | Draw-mode and MT latch control |
| Panel mode / status register | `0x18001224` | Mode transition polling |
| Bare leave-keyboard scenario set | `0xA6` addr `0x03000000` | Stops firmware keyboard typing |
| MT scenario path indicator | `0xA6` GET byte1 `3` | Indicates MT-oriented scenario state |

## Included documents

| File | Purpose |
|------|---------|
| `docs/SYSTEM_SPEC.md` | Technical system specification |
| `docs/ARCHITECTURE.md` | Implementation structure and ownership rules |
| `docs/LINUX_MT_STEPS.md` | Linux multitouch port and validation procedure |
| `docs/PROTOCOL_WINDOWS.md` | Windows-validated coordinator protocol reference |
| `docs/WINDOWS_CAPTURE.md` | Windows reverse-engineering capture method |
| `kernel/README.md` | Kernel driver implementation reference |

## Recommended interpretation

Use this package as a static lookup reference. Treat all values and behaviors as
repository-state documentation, not as a guarantee of completion. Hardware
validation on the target device remains mandatory for any resumed work.
