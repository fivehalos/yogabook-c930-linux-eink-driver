# HID Report Reference

## Scope

This document captures the currently known HID report formats exposed by the
YogaBook C930 E-Ink subsystem.

It is limited to report formats and observed semantics.

## Known report IDs

| Report ID | Role | Status |
|-----------|------|--------|
| `0x90` | single-contact touch | observed and parsed |
| `0x91` | pen | observed in descriptors; not fully documented here |
| `0x0c` | multitouch | observed in descriptors and Windows traces |

## Single-contact touch report (`0x90`)

Known report size:

- 11 bytes

Observed layout:

| Byte | Meaning |
|------|---------|
| 0 | report ID `0x90` |
| 1 | unknown / unused in current notes |
| 2 | contact slot or contact ID |
| 3 | mode byte |
| 4 | action marker |
| 5..6 | raw x, little-endian |
| 7..8 | raw y, little-endian |
| 9..10 | raw pressure or z, little-endian |

Known mode-byte interpretation:

| Value | Meaning |
|-------|---------|
| `0x01` | contact down |
| `0x02` | contact up |
| other | move/update |

Observed action-marker interpretation:

- `0` appears to mark a new action

This report is the main signal used by the current single-contact draw/touchpad
path.

## Multitouch report (`0x0c`)

Observed frame format:

| Byte | Meaning |
|------|---------|
| 0 | report ID `0x0c` |
| 1 | constant or padding byte |
| 2 | contact count |
| 3.. | repeated contact entries |

Known contact-entry size:

- 7 bytes per contact

Known contact-entry layout:

| Offset within contact | Meaning |
|-----------------------|---------|
| 0 | tip flags |
| 1 | contact ID |
| 2 | constant/padding |
| 3..4 | x, little-endian |
| 5..6 | y, little-endian |

Observed tip-flag behavior:

- bit 0 acts as a tip-switch/contact-present flag
- contact entries without that bit set should be treated as lifted

Observed axis ranges from descriptor evidence:

- one axis max around 4319
- the other axis max around 7679

Current notes suggest these correspond to the panel's native coordinate space
with swapped orientation relative to the display axes.

## Coordinate systems

At least two coordinate spaces are in play:

1. native touch coordinates
2. displayed panel coordinates

Current evidence indicates:

- native touch space is higher resolution than display space
- one known native range is approximately `7680 x 4320`
- displayed panel space is `1920 x 1080`
- multitouch report axes appear swapped relative to older `0x90` assumptions

Any rebuild should document coordinate transforms separately from HID parsing,
because the HID report format itself does not define screen rotation or mapping.

## Practical distinction

The single-contact `0x90` path and multitouch `0x0c` path should be treated as
different firmware outputs, not as two encodings of the same logical mode.

Current evidence says:

- `GET=0` commonly correlates with `0x90`
- `GET=3` plus the correct display-config bit correlates with live `0x0c`

That correlation is useful operationally, but it is still an observed behavior,
not yet a fully explained firmware contract.
