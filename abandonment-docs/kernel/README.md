# Kernel Driver Reference — `eink_drm`

## 1. Component identity

| Item | Value |
|------|-------|
| Module target | `eink_drm.ko` |
| Domain | Linux kernel DRM/KMS driver |
| Controlled hardware | Lenovo YogaBook C930 E-Ink panel USB display path |
| Interface bound | Vendor bulk interface of `048d:8951` |
| Language | C |

## 2. Driver objective

Expose the E-Ink panel as a native DRM output and execute panel updates through
the vendor USB protocol without using the legacy `/dev/eink0` architecture.

## 3. Implemented responsibilities

| Responsibility | Status |
|----------------|--------|
| USB probe and device-state setup | Implemented |
| GET_SYS / protocol doorknock | Implemented |
| Draw-mode entry / keyboard exit path | Implemented in current repository state |
| Panel RAM load (`0xA8`) | Implemented |
| Display update / blit (`0x94`) | Implemented |
| DRM connector and mode exposure | Implemented |
| Atomic framebuffer update path | Implemented |

## 4. Internal structure

| Path or file area | Responsibility |
|-------------------|----------------|
| `eink_drm_drv.c` | Probe, lifecycle, module parameters, USB-level state |
| `eink_panel.h` | Product constants, dimensions, USB IDs |
| `protocol/ite8951.h` | Opcode, struct, and API definitions |
| `protocol/ite8951_usb.c` | Command transport, register I/O, mode steps, blit path |
| `drm/eink_drm_kms.c` | DRM object creation and framebuffer update logic |
| `drm/eink_drm_drv.h` | Shared internal driver state |

## 5. Protocol requirements

| Requirement | Description |
|-------------|-------------|
| Async bulk OUT | Required due to panel behavior |
| Persistent bulk IN handling | Required for reliable response and status drain |
| USBS drain after each command | Required to avoid wedged follow-up traffic |
| Fixed xfer base | Use `0x00382f30` for panel RAM upload and blit addressing |

## 6. Validated constants

| Item | Value |
|------|-------|
| Panel resolution | 1920 x 1080 |
| Transfer base address | `0x00382f30` |
| MT / draw config register | `0x18001138` |
| Mode status register | `0x18001224` |
| Typical transfer chunk ceiling | 61440 bytes |
| Current visible blit waveform payload | `0xff` |

## 7. Addressing rule

| Rule | Meaning |
|------|---------|
| GET_SYS image buffer is not the transport base | Do not use reported `image_buf` for LD_IMG / BLIT addressing |
| Blit addressing uses fixed stride assumptions | Current validated logic assumes stride 1920 |

## 8. DRM model

| Aspect | Current interpretation |
|--------|------------------------|
| Connector role | Standard DRM output |
| Commit model | Atomic commit via simple display pipe |
| Frame source | Imported framebuffer / dumb buffer path |
| Output semantics | E-Ink-specific refresh characteristics handled inside driver |

## 9. Non-responsibilities

| Excluded responsibility | Assigned elsewhere |
|-------------------------|-------------------|
| Final input ownership and translation | Userspace |
| Keyboard layout logic | Userspace |
| D-Bus mode API | Userspace |
| Lenovo protocol provenance | Documentation / reference archives |

## 10. Known gaps

| Gap | Impact |
|-----|--------|
| Dirty-region diffing incomplete | Full-frame updates remain the likely baseline |
| Ghosting budget policy incomplete | Display quality management unfinished |
| Suspend/resume incomplete | Reliability gap |
| Boot packaging incomplete | Deployment not finalized |
| Linux MT mode sequencing incomplete | Input/display coexistence unfinished |

## 11. Required operational cautions

| Caution | Reason |
|---------|--------|
| Do not load legacy reference module simultaneously | Both target overlapping hardware paths |
| Do not replace fixed xfer base without hardware proof | Known regression risk |
| Do not route production pixels from userspace | Violates architecture and duplicates kernel responsibility |
| Do not assume LCD-style refresh expectations | E-Ink timing and waveform constraints dominate |

## 12. Validation outcomes recorded by the repository

| Outcome | Repository claim |
|---------|------------------|
| USB protocol bring-up | Validated |
| First visible pixel output | Validated |
| Compositor-driven E-Ink output | Validated |
| Native Linux multitouch | Not yet validated in production path |
| Production-ready refresh policy | Not yet validated |

## 13. Next-step priorities if resumed

| Priority | Work item |
|----------|-----------|
| 1 | Stabilize packaging and boot integration |
| 2 | Complete refresh policy and ghost management |
| 3 | Add Linux MT arm support compatible with ongoing display ownership |
| 4 | Finalize suspend/resume and reconnect behavior |

## 14. Archive note

This file is a technical lookup reference for the kernel driver area. It is
intended to preserve structure and invariant knowledge needed for future
reconstruction or migration.
