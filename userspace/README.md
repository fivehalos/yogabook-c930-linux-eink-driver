# Userspace (C)

All components in `userspace/` are **plain C** (C11). See
[docs/CODING_STYLE.md](../docs/CODING_STYLE.md).

## Components

### eink-touchpad (trial)

Full-screen touchpad emulator — first input trial before `einkd`.

```
hidraw (touch 0x90) → eink-touchpad → uinput → libinput → compositor
```

- **1 finger** — relative pointer motion + tap-to-click
- **2 finger swipe** — arrow / page keys
- **3 finger swipe** — home / end / back / forward

Build and run (**must exit firmware keyboard mode first** — see below):

```bash
cd userspace/eink-touchpad
make

# Load production DRM driver and exit hidkb firmware mode:
cd ../../kernel/eink_drm && make
sudo rmmod eink_drm 2>/dev/null; sudo rmmod eink 2>/dev/null
sudo insmod ./eink_drm.ko drm_enable=1 drm_stage=5 exit_keyboard_on_load=1 \
    panel_test_pattern=0

journalctl -k -b | tail -20 | grep -iE 'draw mode|keyboard'

sudo ./eink-touchpad --debug
```

Drawing a blank panel is **separate** from input: `exit_keyboard_on_load=1` (or a
compositor DRM commit) calls `ite8951_exit_keyboard_mode` in the kernel driver.
Restore firmware keyboard with reboot or `reference/linux-2019/enable-eink-kb.sh`.

### einkd (planned)

Mode orchestrator daemon (D-Bus `org.yogabook.Eink`).

- Modes: `keyboard` | `reader` | `draw` | `display` | `off`
- Owns HID touch interface; does **not** own pixels (DRM driver does)
- Keyboard emulator: layout JSON → image on DRM output + touch → uinput
- Sleep/resume coordination with kernel driver

### Plugins (C libraries / tools)

- `eink-kbd` — layout engine
- `eink-reader` — PDF/EPUB, page turns
- `eink-draw` — pen canvas

### eink-term (optional, C)

E-Ink-aware terminal: DECSET 2026 batching, no blink cursor, refresh throttling.
Stock terminals work once DRM output exists; this is an enhancement.

### Editor integration (out of tree)

Neovim config (pagination, blind mode) may live in separate repos — not part of
the C driver stack.

## Shared headers

`userspace/common/` — panel dimensions and touch types shared across tools.

## Status

- **eink-touchpad** — initial trial (Phase 3 input path)
- **einkd** — not started; see [docs/BLUEPRINT.md](../docs/BLUEPRINT.md) Phases 3–6
