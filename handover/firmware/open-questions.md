# Open Questions

This document tracks what is still unknown or only partially explained in the
firmware interface.

## Highest-priority unknowns

### Reliable keyboard re-entry

Known:

- leaving keyboard mode is reasonably well understood
- re-entering keyboard mode without relying on Lenovo software is not

Unknown:

- the exact sequence needed to return from query state `0` or `3` to a stable
  firmware-keyboard state

## Scenario semantics

Known:

- query result `1` means firmware keyboard
- query result `0` is a non-keyboard leave state
- query result `3` correlates with pen-mouse or multitouch latch behavior

Unknown:

- whether `0`, `1`, and `3` are the complete scenario set
- whether `0x01030100` is a compound scenario encoding or a different class of
  command tunneled through `0xA6`

## `0xB3` field map

Known:

- `0xB3` exists in both inline-CDB and payload-based forms
- specific tuples such as `0100 / 0003 / 0301` and `0100 / 0003 / 0201` are
  associated with the multitouch-entry path

Unknown:

- exact field meanings
- whether both forms drive the same internal firmware structure
- which fields are mandatory versus advisory

## `0xAE` semantics

Known:

- `0xAE arg1=0x0100` appears in the stronger multitouch-entry sequence

Unknown:

- command purpose
- payload or side effects beyond the observed correlation

## Touch-routing interactions

Known:

- a payload-based `0xAC`/`0xB3`/`0xAF` sequence enables a useful custom
  single-contact path
- that same route appears to suppress or replace the real multitouch `0x0c`
  path when applied after multitouch latch

Unknown:

- whether both routes can coexist cleanly
- whether a different `0xAF` layout can preserve `0x0c`
- whether the single-contact route is a fallback mode or a separate owner
  contract

## Buffer-base model

Known:

- visible output works with upload/blit base `0x00382f30`
- `GET_SYS` exposes buffer addresses

Unknown:

- why the reported `GET_SYS` buffer fields do not directly define the visible
  update base in the currently working experiments
- whether there are multiple RAM windows, aliases, or mode-dependent bases

## Display-status structure

Known:

- `0xB1` returns enough information to expose an `engine_busy` flag

Unknown:

- full layout of the 136-byte response
- whether it also reports waveform, queue depth, region state, or timestamps in
  a reusable way

## HID surface completeness

Known:

- `0x90` and `0x0c` are partially understood
- `0x91` pen reporting exists

Unknown:

- full pen report format
- stylus button/pressure semantics
- whether there are other vendor reports relevant to mode switching
