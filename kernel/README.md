# Kernel — DRM display driver

**Target:** `eink_drm.ko` — DRM/KMS driver for USB `048d:8951` vendor interface.

## Goals

- Register `card0-Eink-1` connector at 1920×1080
- Accept standard DRM atomic commits (dumb buffers)
- Internally: diff frames, select waveform, blit dirty rects, wait for engine
- Manage ghosting budget (periodic GC16 cleanse)
- Proper suspend/resume

## Starting points

- **Hardware knowledge:** `reference/` archives (gitignored) — opcode tables,
  USB notes, HID formats. Read only; do not port into production code.
- **Linux APIs:** `drivers/gpu/drm/` examples, simple USB display: `udl` driver
- **Policy:** [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)

## Not in scope

- HID touch/keyboard (userspace `einkd`)
- Mode orchestration (userspace)
- `/dev/eink0` text protocol (reference driver only; not production)

## Status

Phase 1 in progress — USB probe + ITE8951 doorknock **validated on C930 hardware**
(2026-07-11). Next: DRM connector + first pixel. See
[docs/BLUEPRINT.md](../docs/BLUEPRINT.md) (agent handoff).
