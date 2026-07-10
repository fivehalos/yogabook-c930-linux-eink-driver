# Lenovo YogaBook C930 E-Ink for Linux

Community driver and blueprint for the **Lenovo YogaBook C930** (YB-J912)
E-Ink keyboard panel — **not** the unrelated Lenovo Yoga C930 laptop.

## Status

This repository is being restructured from a 2019 proof-of-concept kernel
module into a **first-class Linux display stack**:

- **DRM/KMS kernel driver** — panel appears as a real display (`Eink-1`)
- **`einkd` userspace daemon** — mode switching (keyboard / reader / draw)
- **E-Ink-aware refresh** — waveforms, partial update, ghosting management
- **Keyboard emulator** — custom layouts via touch + uinput (post-boot)

Reference material (2019 Linux driver, Lenovo Windows source) is **not shipped
in this repo**. Fetch locally when needed — see [reference/README.md](reference/README.md).

## Quick links

| Document | Description |
|----------|-------------|
| [**Blueprint & roadmap**](docs/BLUEPRINT.md) | Architecture, conclusions, phased plan |
| [Reference fetch guide](reference/README.md) | How to download archives locally |

## Hardware

USB device `048d:8951`, 1920×1080 E-Ink panel, multiple interfaces:

- **Vendor bulk** — ITE8951 display controller (init, blit, waveforms)
- **HID** — touch (`0x90`), pen (`0x91`), keyboard events

Main laptop display, Wi-Fi, audio, and primary touchscreen/Wacom stylus work
on stock Linux. This project targets the **E-Ink panel only**.

## Repository layout

```
docs/BLUEPRINT.md     ← start here
kernel/               ← DRM driver (in development)
userspace/            ← einkd, terminal, plugins (planned)
reference/            ← gitignored archives; see reference/README.md
scripts/fetch-references.sh
```

## Trying the 2019 driver (experimental)

Fetch the 2019 driver first: `./scripts/fetch-references.sh --linux`

Then see `reference/linux-2019/README.md`.

**Warning:** immature, sleep issues, use at your own risk.

## Contributing

Read [docs/BLUEPRINT.md](docs/BLUEPRINT.md) for architecture and roadmap.
Active development targets `kernel/` and `userspace/`.

## License

GPL v2 (original Linux driver). Windows reference is GPLv3 (Lenovo) — see
`reference/windows-lenovo/`.

## Acknowledgements

- [aleksb/yogabook-c930-linux-eink-driver](https://github.com/aleksb/yogabook-c930-linux-eink-driver) — 2019 USB protocol RE
- Lenovo — open-source Windows E-Ink stack (`YOGA.BOOK.2.Eink.Reader`)
