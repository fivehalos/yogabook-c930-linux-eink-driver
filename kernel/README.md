# Kernel — DRM display driver

**Target:** `eink_drm.ko` — DRM/KMS driver for USB `048d:8951` vendor interface.

## Goals

- Register `card0-Eink-1` connector at 1920×1080
- Accept standard DRM atomic commits (dumb buffers)
- Internally: diff frames, select waveform, blit dirty rects, wait for engine
- Manage ghosting budget (periodic GC16 cleanse)
- Proper suspend/resume

## Starting points

- Protocol implementation: `reference/linux-2019/driver/eink.c` (fetch via `scripts/fetch-references.sh`)
- Opcode / struct definitions: `reference/windows-lenovo/.../inc/tconcmd.h`
- Linux DRM examples: `drivers/gpu/drm/`, simple USB display: `udl` driver

## Not in scope

- HID touch/keyboard (userspace `einkd`)
- Mode orchestration (userspace)
- `/dev/eink0` text protocol (retire for production; optional debug interface)

## Status

Not yet started. See [docs/BLUEPRINT.md](../docs/BLUEPRINT.md) Phase 1.
