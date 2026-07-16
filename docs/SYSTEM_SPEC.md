# YogaBook C930 E-Ink technical specification

This document is a firmware-facing technical description of the Lenovo
YogaBook C930 E-Ink subsystem as currently mapped from captures, reference
material, and direct hardware observation.

It is not a design note for the current prototype. It describes the device,
its externally visible protocol, its input/display personalities, and the
Linux subsystems an eventual clean solution will need to integrate with.

Related references:

- [ARCHITECTURE.md](ARCHITECTURE.md) - codebase and layer ownership
- [PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md) - focused Windows wire notes
- [LINUX_MT_STEPS.md](LINUX_MT_STEPS.md) - MT reverse-engineering notes
- [../kernel/eink_drm/README.md](../kernel/eink_drm/README.md) - kernel bring-up
- [../userspace/README.md](../userspace/README.md) - userspace components

---

## 1. System description and USB devices

### 1.1 Functional model

The YogaBook C930 exposes two user-facing surfaces:

- **LCD panel** - standard primary display path
- **E-Ink panel** - secondary display/input subsystem with:
  - vendor USB display/control transport
  - firmware-rendered keyboard mode
  - non-keyboard pen/touch personalities
  - HID-facing input paths separate from the display transport

### 1.2 Linux-visible devices

Observed hardware identity:

- USB vendor/product: **048d:8951**
- vendor bulk interface for display/protocol control
- additional HID interfaces for touch / pen / related input functions

Observed logical split of the USB functions:

| Function | Typical interface | Linux-facing form | Notes |
|----------|-------------------|-------------------|-------|
| Display + control protocol | iface 0 | USB vendor/bulk device | owner-draw, scenario control, register access |
| Single-touch route | iface 1 | HID / hidraw / evdev | carries `0x90` single-contact reports |
| MT digitizer route | iface 3 | HID / hidraw / evdev | carries the MT report family |

Linux device-node numbering is not stable across boots. Any eventual solution
must identify interfaces by USB topology, descriptor content, interface number,
and/or observed traffic, not by hard-coded `hidrawN` or `eventN` values.

### 1.3 Linux integration surfaces

An eventual Linux solution will need to attach to these subsystems:

- **USB vendor bulk transport** for display/control protocol
- **DRM/KMS** for the E-Ink output path
- **hidraw** for raw HID digitizer access
- **evdev** for kernel-parsed input visibility and coexistence analysis
- **uinput** for synthetic keyboard/touchpad export
- **libinput / compositor input stack** for desktop integration

These are integration targets, not a statement about the current prototype.

---

## 2. Protocol description

### 2.1 Transport

The display/control transport is USB bulk BOT-style framing on MI_00:

- OUT endpoint: **0x02**
- IN endpoint: **0x81**

Each exchange is:

1. `USBC` control packet OUT
2. optional data phase
3. `USBS` status IN

Important rule:

- **Always drain `USBS`** after every exchange

If the status phase is skipped, later commands wedge.

### 2.2 Control packet summary

Protocol framing is documented in more detail in
[PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md), but the operational model is:

- 31-byte command packet
- SCSI opcode `0xFE`
- BE32 address field
- 4 BE16 argument fields
- optional IN or OUT payload

### 2.3 Mapped opcodes

| Opcode | Role | Mapping status |
|--------|------|--------|
| `0x80` | GET_SYS / doorknock | partially mapped |
| `0x81` | READ_MEM | observed, purpose local to mode transitions |
| `0x83` | READ_REG | mapped |
| `0x84` | WRITE_REG | mapped |
| `0x94` | display update / blit | mapped enough for owner-draw |
| `0xA6` | scenario GET / SET | strongly mapped |
| `0xA8` | load image area | mapped enough for owner-draw |
| `0xA9` | waveform set | partially mapped |
| `0xAC` | handwriting region / clear | partially mapped |
| `0xAE` | MT-related transition | partially mapped |
| `0xAF` | TP area routing | partially mapped |
| `0xB3` | dynamic settings | partially mapped; multiple encodings observed |

### 2.4 Scenario model (`0xA6`)

Observed scenario outcomes:

| GET byte1 | Meaning |
|-----------|---------|
| `1` | firmware keyboard |
| `0` | bare non-keyboard / draw leave |
| `3` | pen-mouse / MT-latched state |

Important distinction:

- **`0x03000000`** leaves keyboard but yields **GET=`0`**
- **`0x01030100`** belongs to the MT-related transition path and yields **GET=`3`**

This distinction is foundational. Treating bare leave as MT is incorrect.

### 2.5 Bare leave-keyboard path

Observed result:

```text
0xA6 SET address 0x03000000
0xA6 GET -> byte1 = 0
```

Semantics:

- leaves firmware keyboard mode
- does not by itself imply MT digitizer enable
- is associated with the single-touch route

### 2.6 MT transition path

The currently mapped model is:

```text
MT enable = scenario-3 transition + finger-enable
```

Where:

- **scenario-3 transition** = the observed sequence that reaches GET=`3`
- **finger-enable** = the post-transition register/setting state required for
  finger MT

#### Scenario-3 transition sequence (current best reconstruction)

Critical scenario SET:

```text
0xA6 address = 0x01030100
```

Observed operation table:

| # | Op | Address | Arg1 | Arg2 | Arg3 | Arg4 |
|---|----|---------|------|------|------|------|
| 1 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | 0 |
| 2 | `0xB3` | 0 | `0x0101` | `0x0003` | `0x0301` | 0 |
| 3 | `0xA9` | 0 | `0x0200` | 0 | 0 | 0 |
| 4 | `0xA6` | `0x01030100` | 0 | 0 | 0 | 0 |
| 5-7 | `0x83`/`0x84` | panel / display_cfg | | | | |
| 8 | `0xAE` | 0 | `0x0100` | 0 | 0 | 0 |
| 9 | `0xAC` | 0 | 0 | 0 | 0 | 0 |
| 10 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | 0 |
| 11 | `0xB3` | 0 | `0x0100` | `0x0003` | `0x0301` | 0 |
| 12 | `0xA9` | 0 | `0x0200` | 0 | 0 | 0 |
| 13 | `0xA6` | `0x01030100` | 0 | 0 | 0 | 0 |
| 14 | `0x81` | `0x00000080` | `0x0010` | 0 | 0 | 0 |
| 15 | `0xB3` | 0 | `0x0100` | `0x0003` | `0x0201` | 0 |
| 16 | `0x80` | `0x38393531` | 1 | 2 | 0 | 0 |
| 17-19 | regs again | | | | | |
| 20 | `0xA6` GET | 0 | | | | |

Expected result:

- scenario GET byte1 becomes **`3`**

That state alone is not sufficient to define full MT enable.

#### Finger-enable sequence

After GET=`3`:

1. `0x81` READ_MEM `@0x80` len 16
2. `0xB3` CDB `0100 / 0003 / 0301`
3. update `display_cfg` register `0x18001138`

Required register effect:

- set `display_cfg[0x00080000]`
- do not confuse that with `0x00000800`

Observed armed value:

- `0x00200000` -> `0x00280000`

### 2.7 HID report formats

#### Single-touch route (`0x90`)

Single-contact stream used by the older draw route:

```text
byte0 = 0x90
...
11 bytes total per report
```

This is the single-contact route associated with non-MT operation.

#### MT route (`0x0c`)

Mapped format:

```text
byte0 = 0x0c
byte1 = pad
byte2 = contact_count
then N contacts, 7 bytes each:
  tip_flags, contact_id, pad, x_le16, y_le16
```

### 2.8 Known protocol pitfalls

- do not treat GET=`3` alone as MT success
- do not call full-screen `TOUCH_PEN` after MT arm if the goal is real MT
- do not confuse payload-structured `0xB3` with the CDB-inline `0xB3` used by
  the E sequence
- do not conflate host-side parsing quirks with firmware facts

---

## 3. Pathway to a full custom keyboard

### 3.1 Functional goal

The eventual custom keyboard path should replace the firmware keyboard UI with
a fully host-owned path:

- host-rendered keyboard image on E-Ink
- host-side hit testing and layout semantics
- host-side Linux key injection

### 3.2 Required technical pieces

1. **Display control**
   - owner-draw access to the E-Ink panel
   - deterministic refresh / waveform policy for keyboard visuals

2. **Touch acquisition**
   - raw access to at least one usable touch personality
   - region mapping in panel coordinates

3. **Layout engine**
   - key geometry
   - layers / modifiers
   - hold / repeat / tap policy
   - visual state machine

4. **Linux input export**
   - uinput keyboard device
   - seat / libinput / compositor visibility

### 3.3 Constraints

- no dependence on Lenovo `EinkSvr`
- no dependence on the firmware-rendered keyboard surface
- no requirement that firmware keyboard and custom keyboard share the same
  routing state

---

## 4. Multitouch bring-up pathway

### 4.1 Functional goal

Use the E-Ink panel as a host-owned touchpad / gesture surface with:

- 1 finger pointer
- 2 finger gesture support
- 3 finger gesture support
- optional pen coexistence later

### 4.2 Firmware-side requirements

The MT pathway requires, at minimum:

1. scenario transition out of firmware keyboard mode
2. scenario state consistent with the MT personality (`GET=3`)
3. finger-enable state in `display_cfg`
4. live MT HID traffic on the digitizer path

### 4.3 Success criteria

Minimum MT success at the firmware/interface level:

- GET=`3`
- firmware keyboard no longer owns typing
- live MT HID reports with contact count >= 2
- display owner path remains usable

### 4.4 Open mapping questions

- exact semantics of `0xAE`
- exact `0xB3` field mapping across CDB-inline vs payload forms
- whether MT traffic is a single canonical report family or multiple equivalent
  host-visible encodings

---

## 5. Linux link-up

### 5.1 Kernel-side integration points

An eventual Linux implementation will need to connect the firmware model to:

- **USB transport driver logic**
  - command framing
  - register access
  - scenario transitions
  - display upload / blit

- **DRM/KMS**
  - output enumeration
  - framebuffer import / update policy
  - refresh / waveform selection

- **power-management / hotplug**
  - suspend/resume
  - reconnect
  - output enumeration stability

### 5.2 Input-side integration points

Input integration will need:

- **hidraw**
  - for raw digitizer traffic inspection and possible production use

- **evdev**
  - for understanding what the kernel HID stack exports
  - for coexistence / suppression policy

- **uinput**
  - for custom keyboard export
  - for touchpad / gesture export if raw HID is consumed in userspace

- **libinput**
  - for compositor-facing touchpad / keyboard behavior

### 5.3 Host responsibilities by function

| Function | Host-side Linux subsystem |
|----------|---------------------------|
| owner-draw display | USB transport + DRM/KMS |
| firmware keyboard replacement | userspace app + uinput keyboard |
| raw touch capture | hidraw and/or evdev |
| touchpad export | uinput + libinput |
| session integration | compositor, seat, udev/logind |

---

## 6. System-specific notes: niri, orientation, transform

### 6.1 Compositor model

The primary compositor target is **niri**. The relevant technical requirement
is that the E-Ink panel behaves like a normal DRM output once the kernel side is
correctly linked.

### 6.2 Output identity

Typical compositor-visible connector identity:

- `USB-1` at `1920x1080`

Repeated load/unload cycles may create ghost connector identities. Any eventual
solution should minimize or eliminate that behavior.

### 6.3 Niri linkage

Relevant system-integration facts:

- niri/logind may hold DRM nodes open on hotplug
- uinput-exported devices must be visible to the seated user
- connector placement and output transform are compositor concerns, not
  protocol concerns

### 6.4 Orientation / transform

Touch reports are expressed in panel-native coordinates, not compositor-space
coordinates.

Known geometry relationship:

- native touch space: `7680 x 4320`
- display space: `1920 x 1080`
- axes are effectively swapped relative to display interpretation

This implies two distinct transforms:

1. **raw touch -> panel display space**
2. **panel display space -> compositor output orientation**

The second transform is system-specific and depends on how the compositor
arranges the E-Ink output relative to the LCD.

### 6.5 Transform requirements

Any eventual solution must provide:

- a mapping from native touch coordinates into displayed panel coordinates
- a mapping from displayed panel coordinates into compositor orientation
- a stable convention for gesture direction relative to the user's physical
  orientation, not merely the raw panel axes

---

## 7. Summary

The YogaBook C930 E-Ink subsystem is a USB-controlled secondary display/input
device with a firmware keyboard personality, a bare non-keyboard leave path,
and a scenario-3 MT-related path. The display side uses a vendor bulk transport
with explicit command/status framing. The input side exposes at least a
single-contact HID route and a multi-contact digitizer route. The critical
protocol distinction is between bare leave (`0x03000000`, GET=`0`) and the MT
transition family (`0x01030100`, GET=`3`) plus finger-enable through
`display_cfg[0x00080000]`. An eventual Linux implementation should treat the
device as three linked problems: owner-draw DRM/KMS output, firmware-state and
scenario control over the vendor protocol, and host-owned input export through
hidraw/evdev/uinput/libinput with explicit compositor transform handling.
