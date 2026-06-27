# Input And Mode-Control Reference

## Scope

This document captures the firmware-visible APIs involved in leaving the stock
keyboard mode, entering non-keyboard scenarios, and routing touch input.

It intentionally does not prescribe a Linux driver or daemon architecture.

## Known mode-control opcodes

| Opcode | Short name | Current understanding |
|--------|------------|-----------------------|
| `0xA6` | scenario | query/set coordinator scenario |
| `0xA9` | waveform | mode-transition waveform control |
| `0xAC` | handwriting region | clear or set handwriting region |
| `0xAE` | unknown mode helper | seen in multitouch entry sequence |
| `0xAF` | touch/pen area | configure touch-report regions |
| `0xB3` | dynamic setting | configure input-routing flags; may be CDB-inline or payload-based |

## Scenario model (`0xA6`)

Current evidence supports two distinct uses of `0xA6`:

1. query current scenario
2. request a scenario transition

### Query

Observed query form:

- opcode `0xA6`
- address `0x00000000`
- response length 4 bytes

Observed result:

- response byte 1 reports the live scenario

Known values:

| Value | Meaning |
|-------|---------|
| `1` | firmware keyboard mode |
| `0` | non-keyboard / owner-draw leave state |
| `3` | pen-mouse or multitouch latch state |

### Set

The best-supported set encoding today is:

```text
address = scenario_id << 24
```

Examples:

| Requested state | Address |
|-----------------|---------|
| keyboard | `0x01000000` |
| leave keyboard / pen-mouse | `0x03000000` |

Important behavior:

- requesting `0x03000000` usually leaves keyboard mode
- after that, a subsequent query often reports `0`, not `3`
- therefore success should not be defined as "query equals requested value"
- the stronger success condition is "query no longer reports keyboard"

## Two non-keyboard states

Current evidence strongly suggests there are at least two different
non-keyboard outcomes:

| State | Query result | Input effect |
|-------|--------------|--------------|
| bare leave-keyboard | `0` | single-contact touch path |
| multitouch/pen-mouse latch | `3` | real multitouch path when fully armed |

This distinction is important. A rebuild should not collapse "not keyboard"
into a single abstract success state.

## Minimal leave-keyboard sequence

Observed minimal sequence that stops firmware typing:

1. optional `0xB3` CDB-inline tuple `0100 / 0103 / 0301`
2. optional `0xA9` arg1 `0x0200`
3. `0xA6` set address `0x03000000`
4. `0xA6` query address `0x00000000`

Expected result:

- query byte 1 becomes `0`

This sequence is sufficient to stop stock keyboard behavior, but it is not
sufficient by itself to prove multitouch is armed.

## Multitouch latch path

Current evidence identifies a stronger path that yields query result `3` and
real multitouch HID traffic when fully armed.

The key known transition uses:

```text
0xA6  address = 0x01030100
```

This appears in the Windows-captured multitouch entry sequence and should be
treated as distinct from the simpler `0x03000000` leave-keyboard request.

Observed associated steps around the `GET=3` path include:

- `0xB3` with inline CDB tuples such as `0101 / 0003 / 0301`
- `0xA9` with `arg1 = 0x0200`
- register reads and writes
- `0xAE arg1 = 0x0100`
- `0xAC` clear
- a later `0xB3` tuple `0100 / 0003 / 0201`

The exact semantic map of those fields is still open, but the sequence itself
is evidence-backed.

## Touch-routing opcodes

### `0xAC` handwriting region

Observed as a small payload-based command used to set or clear a handwriting
region. A zero region is commonly used during draw/owner transitions.

Known payload fields:

- x
- y
- width
- height

Current payload byte order appears little-endian 16-bit per field.

### `0xAF` touch/pen area

Observed as a payload-based command defining a touch region and flag.

Known payload fields:

- x, y, width, height: 32-bit little-endian
- index: 8-bit slot number
- flag: 8-bit mode

Known flag values:

| Value | Meaning |
|-------|---------|
| `0x00` | no report / cleared slot |
| `0x03` | touch+pen reporting |

This opcode is useful for single-contact custom routing, but current evidence
suggests it can interfere with the real multitouch `0x0c` path if applied after
multitouch entry.

### `0xB3` dynamic setting

Two encodings are currently known:

1. inline CDB arguments with no OUT payload
2. structured OUT payload carrying several booleans/flags

The inline form is strongly associated with the Windows multitouch-entry path.
The payload form is strongly associated with custom owner-draw touch routing.

These should be treated as related but not yet interchangeable APIs.
