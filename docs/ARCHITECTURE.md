# Architecture & implementation policy

This document defines how code in this repository is structured and what we
**do not** ship.

---

## Reference material is not source code

The archives under `reference/` (2019 Linux driver, Lenovo Windows tree) exist
**only to understand hardware behaviour**: USB opcodes, packet layout, register
addresses, HID report formats, waveform names, and init sequences.

| Reference | Use for | Do not |
|-----------|---------|--------|
| `reference/linux-2019/` | Opcode sequences, xfer/blit semantics, USB framing notes | Copy `eink.c`, `/dev/eink0` protocol, or module structure |
| `reference/windows-lenovo/` | `tconcmd.h` opcode table, struct layouts, HID parsing | Port Windows code, D2D UI, SumatraPDF, `EinkSvr` IPC |

**All production code is written clean-room in C for Linux:**

- DRM/KMS kernel driver in `kernel/eink_drm/`
- Daemons and tools in `userspace/` (`einkd`, plugins)
- Style: [CODING_STYLE.md](CODING_STYLE.md)

When reference and hardware disagree, **hardware wins**. Validate on the YogaBook
C930.

---

## Layer ownership

```
Apps (terminal, neovim, reader)
        │  DRM/KMS, standard input
        ▼
einkd (userspace/)     — modes, touch, keyboard emulation, sleep hooks
        │  D-Bus org.yogabook.Eink
        │  does NOT own pixels
        ▼
eink_drm.ko (kernel/)  — connector Eink-1, atomic commits, refresh policy
        │  protocol/ sub-layer talks USB
        ▼
USB 048d:8951          — vendor bulk (display) + HID (touch/pen/keyboard)
```

| Layer | Owns | Must not |
|-------|------|----------|
| `kernel/eink_drm/protocol/` | ITE8951 USB framing, opcodes, panel commands | DRM connector logic |
| `kernel/eink_drm/drm/` | Modes, dumb buffers, atomic commit, diff, ghosting | Direct USB from DRM files |
| `userspace/einkd/` | Mode switching, HID claim, uinput, D-Bus | xfer/blit, `/dev/eink0` writes |
| `scripts/` | Bring-up, fetch references, **temporary** hardware probes | Production driver logic |

---

## Repository layout (target)

```
kernel/eink_drm/
  protocol/     # ite8951 USB — clean-room from reference docs
  drm/          # DRM device, connector, refresh engine
userspace/
  einkd/        # daemon
  plugins/      # kbd, reader, draw (later)
docs/
  BLUEPRINT.md  # roadmap
  ARCHITECTURE.md
scripts/        # test-board.sh (legacy probe until DRM smoke test exists)
packaging/      # udev, systemd, modprobe.d (later)
reference/      # gitignored — never compiled into production builds
```

---

## Retired patterns (production)

These exist only in reference archives or temporary bring-up scripts:

- `/dev/eink0` text commands (`init`, `kb`, `draw`, `xfer`, `blit`)
- `eink.ko` character device driver
- Windows `EinkSvr` / `itetcon.dll` architecture
- Copy-paste from `eink.c` or Windows C++ sources

`scripts/test-board.sh` may load the reference module for **hardware validation
until** `eink_drm` has its own smoke test. It is not part of the target stack.

---

## Shared contracts (not shared code)

Kernel and userspace do not share a C library (GPL boundary). Align via:

- DRM connector properties (`WAVEFORM`, `FORCE_CLEANSE`, etc.) — documented in kernel
- D-Bus API `org.yogabook.Eink` — documented in userspace
- Opcode/register tables — documented in `kernel/eink_drm/protocol/ite8951.h`
- C style — [CODING_STYLE.md](CODING_STYLE.md)

---

## E-Ink UI rules (text workflows)

Product rules for anything rendering on the panel (see also
[BLUEPRINT.md](BLUEPRINT.md)):

- No cursor blink; static bar or blind mode
- Pagination / half-screen scroll, not line-scroll
- DU2 for keystroke feedback; GL16 for text; GC16 cleanse on a budget
- Convergent navigation over arrow-key loops
