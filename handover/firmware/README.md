# YogaBook C930 Firmware Reference

This directory is a clean-room technical reference for the Lenovo YogaBook
C930 E-Ink firmware interfaces.

Its purpose is to support a rebuild from first principles:

- document what the device appears to expose
- separate observed behavior from guesses
- avoid treating the current Linux code as the specification
- give future implementation work a stable hardware-facing contract

## Scope

These notes describe firmware-visible APIs only:

- USB bulk transport for the ITE8951 display controller
- known opcodes, arguments, and packet framing
- scenario and mode-control behavior
- known registers and bitfields
- HID report formats for touch, pen, and multitouch

They do not define:

- Linux kernel architecture
- userspace daemon design
- compositor or UI behavior
- code structure for any future implementation

## Evidence policy

Each statement in this reference should fit one of these classes:

1. Observed on hardware
2. Observed in Windows USB/HID captures
3. Observed in read-only reference archives
4. Hypothesis or open question

When behavior differs between sources, hardware observation wins.

## Documents

- `usb-transport.md`: bulk framing, control packet layout, exchange rules
- `display-controller.md`: display-controller opcodes, payloads, memory model
- `input-modes.md`: scenario control, keyboard leave, draw/owner paths
- `hid-reports.md`: touch and multitouch HID report layouts
- `registers.md`: known registers and currently understood bits
- `open-questions.md`: unresolved behavior, missing field maps, rebuild risks

## Non-goals

This directory should not accumulate implementation lessons such as Linux driver
layout, helper function names, module parameters, or bring-up scripts. If such
details are useful, they belong elsewhere.
