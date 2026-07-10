# YogaBook C930 E-Ink — Project Blueprint

**Device:** Lenovo YogaBook C930 (YB-J912) — **not** the Lenovo Yoga C930 laptop.

**Goal:** Make the E-Ink panel a first-class Linux display with usable keyboard,
text-optimised workflows, and room for custom layouts, reader, and draw modes.

---

## Executive summary

The YogaBook C930 E-Ink panel is a **USB composite device** (`048d:8951`) with
three logical subsystems on separate interfaces:

| Subsystem | USB interface | Role |
|-----------|---------------|------|
| ITE8951 display controller | Vendor bulk (`0xFF`) | Init, waveforms, pixel upload, blit |
| Touch controller | HID (`0x90` touch, `0x91` pen) | Finger/stylus input |
| Keyboard firmware | HID keyboard | Touch→keycode in keyboard mode (pre-OS capable) |

A 2019 community driver proved the **USB display protocol** is correct. Lenovo's
open-source Windows tree (`reference/windows-lenovo/`) confirms opcode tables and
API contracts. Critical runtime pieces (`EinkSvr.exe`, `itetcon.dll`) remain closed.

**We are not porting Windows.** We are building a Linux stack where the kernel
registers a real DRM display, the driver owns E-Ink refresh policy, and userspace
handles input modes (keyboard emulator, reader, draw).

---

## What we know

### Validated (high confidence)

- USB packet framing (USBC/USBS) and opcode table (`0x80`–`0xB3`)
- Init / doorknock, keyboard enable, draw mode, xfer, blit sequences
- BLIT payload structure (`TDRAW_UPD_ARG_DATA`)
- Touch HID report format (`USBHIDAPI.cpp`)
- Waveform modes: INIT, DU2, GC16, GL16
- 1920×1080, 16 grey levels, 16 display pipeline engines
- Partial update + diff-block rendering (Windows `EiUpdate.cpp`)

### Partially known

- Sleep/resume orchestration (message IDs in `SvrMsg.h`, logic in closed `EinkSvr`)
- Keyboard layout blob format (`kb_blob` in `tconcmd.h`)
- Full register map (two addresses known: `0x18001224`, `0x18001138`)

### Closed / missing

- `EinkSvr.exe` source (init state machine, reconnect, app lifecycle)
- `itetcon.dll` implementation (byte-level sequences for some `Tcon*` calls)
- Pre-OS keyboard layout is firmware — custom layouts need post-boot emulation

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Apps: terminal, neovim, reader, draw, (optional compositor) │
├─────────────────────────────────────────────────────────────┤
│  einkd — mode orchestrator (D-Bus)                          │
│    • keyboard | reader | draw | display | off               │
│    • owns HID touch fd, keyboard emulator plugin            │
│    • does NOT own pixels (DRM driver does)                    │
├─────────────────────────────────────────────────────────────┤
│  DRM/KMS driver (eink_drm.ko) — FIRST-CLASS DISPLAY         │
│    • connector: card0-Eink-1 @ 1920×1080                    │
│    • atomic commits from compositor / fbcon                 │
│    • internal: diff, waveform pick, blit, engine wait       │
│    • ghosting budget, periodic GC16 cleanse                 │
├─────────────────────────────────────────────────────────────┤
│  USB 048d:8951                                              │
│    vendor bulk (display)  +  HID (touch/pen/keyboard)       │
└─────────────────────────────────────────────────────────────┘
```

### Design principles

1. **First-class display** — kernel DRM connector, not `/dev/eink0` text hacks.
2. **E-Ink smarts in the driver** — diff, waveforms, ghosting live in DRM commits.
3. **Modes in userspace** — `einkd` switches keyboard/reader/draw; doesn't render.
4. **Text-native UI** — no cursor blink, pagination over scroll, convergent navigation.
5. **Custom keyboards via emulation** — touch HID + layout map + uinput (post-boot).
6. **Don't replicate Windows** — no D2D, no SumatraPDF fork, no 80-message IPC bus.

### E-Ink UI rules (text workflows)

- No cursor blink; static bar or blind mode
- Pagination / half-screen scroll, not line-scroll
- DU2 for keystroke feedback; GL16 for text; GC16 cleanse every N partials
- Convergent navigation (search, paragraph jump) over arrow-key loops
- Optional: typewriter mode, explicit WYSIWYG refresh on demand

See [Kragen Sitaker's e-ink editor design notes](https://dercuano.github.io/notes/eink-design.html).

---

## Roadmap

### Phase 0 — Foundation (current)

- [x] Reference layout documented (`reference/README.md`, gitignored)
- [x] Fetch script (`scripts/fetch-references.sh`)
- [x] Document blueprint (this file)
- [ ] Validate hardware: live USB boot, `lsusb`, confirm interfaces

### Phase 1 — Display driver core

**Milestone:** Panel shows pixels reliably; survives one reboot.

- [ ] Port `reference/linux-2019/driver/eink.c` protocol to `kernel/eink_drm/`
- [ ] Register DRM device, connector, 1920×1080 mode
- [ ] Dumb buffer + atomic commit path
- [ ] Waveform support: INIT, DU2, GL16, GC16
- [ ] Display-ready wait (`0xB1` / `TconWaitDpyReady` equivalent)
- [ ] Fix suspend/resume (replace empty `eink_resume`)
- [ ] Module load order / udev rules for `048d:8951`

**Test:** `modetest`, fbcon, or simple DRM test app paints greyscale image.

### Phase 2 — Smart refresh

**Milestone:** Partial updates without accumulating ghosting.

- [ ] Shadow framebuffer in driver; diff on commit
- [ ] Tile-based dirty region merge (port `EiUpdate.cpp` logic)
- [ ] Per-region waveform selection
- [ ] Ghosting budget → automatic GC16 cleanse
- [ ] DRM connector properties: `WAVEFORM`, `FORCE_CLEANSE`

**Test:** Update small region repeatedly; ghost clears on schedule.

### Phase 3 — Input daemon (`einkd`)

**Milestone:** Stock QWERTY keyboard works; sleep recovery works.

- [ ] Claim HID touch interface (`0x90`/`0x91`)
- [ ] Keyboard mode: firmware KB **or** draw mode + layout emulator
- [ ] `uinput` virtual keyboard
- [ ] D-Bus API: `org.yogabook.Eink` — SetMode, Status
- [ ] systemd: boot load, sleep hook (`REOPEN_8951` logic)
- [ ] Coordinate transform (touch 7680×4320 → display 1920×1080)

**Test:** Type in fbcon or terminal on E-Ink output; close lid and recover.

### Phase 4 — Custom keyboard layouts

**Milestone:** Numpad-only layout works.

- [ ] Layout definition format (JSON: key rects → keycodes)
- [ ] Layout renderer via DRM commit
- [ ] Touch→key mapper in `einkd`
- [ ] Layout switcher (D-Bus / CLI)

### Phase 5 — Text terminal on E-Ink

**Milestone:** `foot` or `eink-term` on DRM output; neovim usable.

- [ ] Terminal on E-Ink DRM connector (stock or `userspace/eink-term`)
- [ ] DECSET 2026 batching, no blink cursor
- [ ] Optional `eink.nvim`: pagination, blind, typewriter modes
- [ ] LSP + basic plugins

### Phase 6 — Reader & draw plugins

- [ ] Reader: PDF via poppler/mupdf, page-turn gestures
- [ ] Draw: pen HID or hardware handwriting (`0xAC`/`0xAE`)

### Phase 7 — Compositor integration (optional)

- [ ] Sway/Hyprland output config for `Eink-1`
- [ ] max_render_time / refresh cap
- [ ] Dual-screen: main LCD + E-Ink reference pane

---

## Repository layout

```
/
├── README.md                 # Project entry point
├── docs/
│   └── BLUEPRINT.md          # This file
├── kernel/                   # DRM driver (active development)
├── userspace/                # einkd, eink-term, plugins (future)
├── reference/                # Local archives (gitignored — see README)
├── scripts/fetch-references.sh
└── LICENSE
```

---

## Key reference files (after local fetch)

Run `./scripts/fetch-references.sh` first. See [reference/README.md](../reference/README.md).

| Topic | Location (local, gitignored) |
|-------|------------------------------|
| Linux USB driver (2019) | `reference/linux-2019/driver/eink.c` |
| USB protocol notes | `reference/linux-2019/usb-protocol.md` |
| Windows opcode table | `reference/windows-lenovo/.../inc/tconcmd.h` |
| Windows TCON API | `reference/windows-lenovo/.../inc/itetcon.h` |
| Touch HID parser | `reference/windows-lenovo/.../EinkIteAPI/USBHIDAPI.cpp` |
| Diff update logic | `reference/windows-lenovo/.../comm/EiUpdate.cpp` |

**Downloads:** [aleksb/yogabook-c930-linux-eink-driver](https://github.com/aleksb/yogabook-c930-linux-eink-driver) · [Lenovo open source (C930)](https://pcsupport.lenovo.com/us/en/products/tablets/yoga-series/yoga-book-c930/downloads/ds503569)

---

## Risks

| Risk | Mitigation |
|------|------------|
| DRM driver porting effort | Start from proven `eink.c` protocol code |
| Modern kernel API drift | Test on target kernel early |
| `usbhid` interface race | udev softdep + systemd ordering |
| Sleep still broken | Implement full re-init sequence from `SvrMsg.h` |
| Secure Boot | Sign module or disable SB |
| GUI on E-Ink | Don't — text-native workflows only |

---

## Success criteria

| Stage | Done when |
|-------|-----------|
| **Usable daily** | Type on E-Ink keyboard, survive sleep, auto-start on boot |
| **Better than Windows** | Custom keyboard layouts, cleaner refresh policy |
| **Developer tool** | Neovim + LSP on E-Ink DRM output |
| **First-class citizen** | `wlr-randr` shows `Eink-1`; fbcon works |
