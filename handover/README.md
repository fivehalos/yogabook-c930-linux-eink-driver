# YogaBook C930 E-Ink — clean-room handover

This folder bundles everything needed to rebuild the firmware-facing stack
without inheriting the current Linux implementation.

**Read first:** `firmware/README.md`

---

## Contents

| Path | What it is |
|------|------------|
| `firmware/` | Clean-room system description (USB transport, display API, input modes, HID, registers, open questions) |
| `win-captures/` | Raw Windows USB/HID reverse-engineering captures and text extracts |
| `windows-lenovo/` | Symlink → `reference/yoga.book.2.e-reader_v1.0.0.5/` (Lenovo GPLv3 open-source tree) |

---

## How to use

1. Start with `firmware/` for the target contract — what the device exposes and
   what is still unknown.
2. Cross-check wire behavior in `win-captures/` (especially `E-usbc-ops.txt`,
   `E-hid-0x85.txt`, `S-usbc-ops.txt`).
3. Use `windows-lenovo/YOGA.BOOK.2.Eink.Reader_v1.0.0.5/` as read-only
   reference for opcode tables and struct layouts — **do not port** Windows code
   into a new implementation.

### Key Windows source files (after symlink resolves)

| File | Use for |
|------|---------|
| `inc/tconcmd.h` | USB opcode table |
| `inc/itetcon.h` | `GET_SYS` / system-info layouts |
| `inc/EinkIteAPI.h` | App-facing TCON API surface |
| `inc/SvrMsg.h` | Sleep/resume message IDs |
| `EinkIteAPI/USBHIDAPI.cpp` | Touch HID report parsing |
| `comm/EiUpdate.cpp` | Partial-update algorithm (reimplement, don't copy) |

Closed binaries (`EinkSvr.exe`, `itetcon.dll`) are **not** in the Lenovo tarball.

---

## win-captures guide

| File | Role |
|------|------|
| `E-usbc-ops.txt` | Parsed USBC opcode sequence — multitouch / pen-mouse entry (ops 1–20) |
| `E-usbc.txt` | Shorter USBC notes |
| `E-hid-0x85.txt` | HID report `0x0c` multitouch traces |
| `E-bulk-capdata.txt` | Bulk capdata extract (may be empty placeholder) |
| `S-usbc-ops.txt` | EinkSvr restart / enable sequence |
| `S-ctrl-out-31.txt` | 31-byte control-packet examples |
| `E-multitouch-penmouse.pcap.*.bak` | Raw USBpcap backups (rename to `.pcap` for Wireshark) |

Capture methodology: `docs/WINDOWS_CAPTURE.md` (repo root).

---

## Refresh this folder

From the repository root:

```bash
./handover/sync.sh
```

Copies `docs/firmware/` and `win-captures/` into `handover/` and recreates the
`windows-lenovo` symlink.

---

## Portable archive

To ship offline (without repo paths):

```bash
tar -czf yogabook-eink-handover.tar.gz \
  handover/firmware \
  handover/win-captures \
  reference/yoga.book.2.e-reader_v1.0.0.5
```

The full Lenovo tree is ~1.1 GB (mostly bundled PDF reader). For opcode/HID
work only `inc/`, `EinkIteAPI/`, and `comm/` are usually sufficient (~1.6 MB).

---

## License

- `firmware/` and capture notes: same as this repository (GPL v2).
- `windows-lenovo/`: GPLv3 — see `COPYING.GPLv3` in the Lenovo package.
