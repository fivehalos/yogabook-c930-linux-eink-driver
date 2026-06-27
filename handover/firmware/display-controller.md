# Display Controller Reference

## Scope

This document describes the currently known display-facing firmware API exposed
through the ITE8951 bulk interface.

It records wire behavior, not Linux implementation choices.

## Known opcodes

| Opcode | Name used in notes | Purpose | Evidence |
|--------|--------------------|---------|----------|
| `0x80` | `GET_SYS` | Query controller/system information | hardware, Windows refs |
| `0x83` | `READ_REG` | Read 32-bit register | hardware, Windows refs |
| `0x84` | `WRITE_REG` | Write 32-bit register | hardware, Windows refs |
| `0x94` | `DPY_AREA` / blit | Display update from controller memory | hardware, Linux refs |
| `0xA8` | `LD_IMG_AREA2` | Load host pixels into controller memory | hardware, Linux refs |
| `0xA9` | `SET_WAVEFORM` | Select waveform/update mode | hardware, Windows refs |
| `0xB1` | `GET_DPY_STATUS` | Query busy/ready state | hardware |

Other opcodes that affect input or mode are documented in
`input-modes.md`.

## GET_SYS (`0x80`)

Known behavior:

- response length: 112 bytes
- control-packet address: `0x38393531`
- args: `0x0001`, `0x0002`, `0x0000`, `0x0000`

Known fields from the response body:

| Offset | Size | Meaning |
|--------|------|---------|
| `0x08` | 4 | signature, observed `0x38393531` |
| `0x10` | 4 | panel width |
| `0x14` | 4 | panel height |
| `0x18` | 4 | update buffer base |
| `0x1c` | 4 | image buffer base |

Important caution: current evidence says the reported image/update buffer values
are not sufficient by themselves to explain the working upload/blit address
scheme used by the panel.

## Pixel memory model

The strongest current evidence says display uploads and blits use a controller
RAM base of:

```text
0x00382f30
```

This value appears in the 2019 Linux reference material and matches current
hardware results. The panel's reported `GET_SYS` image buffer address has not
yet proven to be the working blit base for visible output.

Any rebuild should treat the address model as:

- known-working: fixed upload/blit base `0x00382f30`
- not yet fully explained: relationship between that base and `GET_SYS`

## Load image (`0xA8`)

Working interpretation:

- host uploads raw grayscale pixel bytes into controller RAM
- control-packet address is `0x00382f30 + buf_offset`
- `arg3` = width
- `arg4` = height
- data phase is a raw pixel payload

Observed payload format:

- 1 byte per pixel
- current working grayscale packing: high nibble carries the gray level
- low nibble is consistently observed as `0x0f` in current experiments

Known transfer limit:

- 61440 bytes per transaction has been a stable working maximum
- this corresponds to `1920 x 32` at 1 byte per pixel

## Display update / blit (`0x94`)

Working behavior:

- uses a 24-byte payload of six big-endian 32-bit fields
- payload fields are:
  - memory address
  - waveform
  - destination x
  - destination y
  - width
  - height

### Blit payload layout

| Offset | Size | Meaning |
|--------|------|---------|
| `0x00` | 4 | source memory address |
| `0x04` | 4 | waveform |
| `0x08` | 4 | destination x |
| `0x0c` | 4 | destination y |
| `0x10` | 4 | width |
| `0x14` | 4 | height |

### Working memory-address formula

Current evidence supports:

```text
mem_addr = 0x00382f30 + buf_offset - x - (1920 * y)
```

where:

- `buf_offset` is the upload offset used with `0xA8`
- `x`, `y`, `width`, `height` are destination rectangle coordinates in panel
  space
- panel stride is 1920 pixels

This formula is evidence-backed but still unusual enough that any rebuild
should preserve it as an explicit documented invariant rather than assuming it
is a generic framebuffer convention.

## Waveforms

Known waveform identifiers:

| Value | Meaning in current notes |
|-------|--------------------------|
| `0` | INIT |
| `1` | DU2 |
| `2` | GC16 |
| `3` | GL16 |
| `0xff` | CURRENT / keep current working mode |
| `0x0200` | mode-transition waveform argument observed around scenario changes |

At present, `0xff` has been the most consistently visible setting for blit
updates. The exact relationship between the low-numbered waveforms and
mode-transition value `0x0200` remains incomplete.

## Display-ready query (`0xB1`)

Known behavior:

- response length observed: 136 bytes
- low-level display status contains an `engine_busy` field
- polling until `engine_busy == 0` has been sufficient before and after blits

Only a small part of this response is currently understood.
