# Register Reference

## Scope

This document lists the controller registers that are currently known to matter
for display and input-mode transitions.

The list is intentionally short. Only registers with concrete evidence should
be added here.

## Known registers

| Address | Name used in notes | Current understanding |
|---------|--------------------|-----------------------|
| `0x18001224` | panel mode | reports panel-ready state during keyboard exit and draw entry |
| `0x18001138` | display configuration | controls draw/input mode bits, including multitouch enable |

## `0x18001224` panel mode

Observed behavior:

- read during keyboard-exit and draw-entry sequences
- value `0x80000000` is treated as a ready state in current notes

Current interpretation:

- the register reflects whether the panel/controller is ready for the next
  stage of the mode transition

Unknowns:

- full bitfield map
- meaning of other observed values
- whether it is purely status or partly latched mode state

## `0x18001138` display configuration

Observed behavior:

- read before draw/input transitions
- written during owner-draw handoff
- bit changes correlate with single-touch versus multitouch routing

Known values or masks:

| Value or mask | Meaning |
|---------------|---------|
| `0x002e0000` | known working draw/owner-display configuration |
| `0x00080000` | multitouch finger-enable bit |
| `0x00280000` | observed multitouch-enabled configuration after OR-ing in `0x00080000` |

Important distinction:

- `0x002e0000` is associated with a known working draw/display handoff
- `0x00280000` is associated with the Windows-validated multitouch latch path

It is not yet safe to assume that these values describe a simple monotonic mode
progression. They may represent overlapping feature bits rather than named
states.

## Read/write API usage

Current working wire usage:

- register read uses opcode `0x83`
- register write uses opcode `0x84`
- register address is carried in the control-packet address field
- 32-bit register data is transferred in big-endian form on the wire

That byte order should be treated as an observed transport fact, not inferred
from host endianness.
