# Userspace

## Planned components

### einkd

Mode orchestrator daemon (D-Bus `org.yogabook.Eink`).

- Modes: `keyboard` | `reader` | `draw` | `display` | `off`
- Owns HID touch interface; does **not** own pixels (DRM driver does)
- Keyboard emulator: layout JSON → image on DRM output + touch → uinput
- Sleep/resume coordination with kernel driver

### eink-term (optional)

E-Ink-aware terminal emulator: DECSET 2026 batching, no blink cursor,
refresh throttling. Enhancement, not requirement — stock terminals work
once DRM output exists.

### Plugins

- `eink-kbd` — layout engine
- `eink-reader` — PDF/EPUB, page turns
- `eink-draw` — pen canvas
- `eink.nvim` — pagination, blind mode, DRM property hints

## Status

Not yet started. See [docs/BLUEPRINT.md](../docs/BLUEPRINT.md) Phases 3–6.
