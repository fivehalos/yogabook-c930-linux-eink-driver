# USB Transport Reference

## Device role

The E-Ink display controller appears on the YogaBook C930 USB composite device
`048d:8951` as a vendor-specific bulk interface. Current captures and Linux
bring-up notes identify:

- bulk OUT endpoint `0x02`
- bulk IN endpoint `0x81`
- max packet size 512 bytes

This interface carries ITE8951 display-control traffic.

## Exchange model

Every controller command uses a three-part exchange:

1. control packet OUT (`USBC`, 31 bytes)
2. optional data phase IN or OUT
3. status packet IN (`USBS`, 13 bytes)

The status phase must be drained after every exchange. Evidence from Windows
and Linux testing indicates that failing to read `USBS` can wedge the next
command until the pending status data is consumed.

Response data and the trailing status packet may arrive:

- as separate IN transfers, or
- coalesced into one IN transfer

Any implementation should therefore parse the response body and look for the
status signature independently.

## Control packet format

Observed control packet size: 31 bytes.

| Offset | Size | Meaning |
|--------|------|---------|
| 0x00 | 4 | ASCII `USBC` |
| 0x04 | 4 | tag bytes, observed `61 89 51 89` |
| 0x08 | 4 | little-endian response length |
| 0x0c | 1 | direction flag, `0x80` if IN data expected, else `0x00` |
| 0x0d | 1 | reserved |
| 0x0e | 1 | CDB length, observed `0x10` |
| 0x0f | 1 | SCSI opcode, observed `0xfe` |
| 0x10 | 1 | LUN, observed `0x00` |
| 0x11 | 4 | big-endian address field |
| 0x15 | 1 | ITE opcode |
| 0x16 | 2 | big-endian arg1 |
| 0x18 | 2 | big-endian arg2 |
| 0x1a | 2 | big-endian arg3 |
| 0x1c | 2 | big-endian arg4 |
| 0x1e | 1 | padding, observed `0x00` |

## Status packet format

Observed status packet size: 13 bytes.

Known success signature:

```text
55 53 42 53 61 89 51 89 00 00 00 00 00
```

That is:

- ASCII `USBS`
- same tag bytes as the request header
- trailing zero status bytes in the successful cases seen so far

The meaning of non-zero trailing status bytes is not yet documented here.

## Address and argument conventions

The transport itself does not assign semantics to the `address` and `arg1..4`
fields. Their interpretation depends on the ITE opcode.

Current evidence shows that these fields are used in at least four distinct
ways:

- register address for register read/write opcodes
- destination or source memory address for display operations
- scenario selector encoded into the high byte of the address field
- compact inline parameter tuples for opcodes such as `0xB3`

## Reliability constraints

Observed constraints relevant to a rebuild:

- a fresh IN reader should be ready before commands that expect response data
- IN and status handling must tolerate short packets
- the panel can leave stale data queued on the IN endpoint after errors
- implementations should provide a drain step before initialization or retry

## Known initialization probe

The most stable known read transaction is `GET_SYS` (`0x80`), sent repeatedly
as a doorknock during initialization. Current evidence indicates:

- response length 112 bytes
- control-packet address `0x38393531`
- args `1, 2, 0, 0`

This sequence is a hardware-observed probe, not yet a full explanation of the
controller boot state machine.
