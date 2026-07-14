# Reference archives (local only)

Reference material is **not tracked in git** and **not ported into production
code**. Use it to learn what the hardware expects (opcodes, registers, HID
formats, init sequences).

```bash
./scripts/fetch-references.sh          # Linux 2019 driver (automatic)
./scripts/fetch-references.sh --windows  # prints Windows download instructions
```

Expected layout after fetching:

```
reference/
├── README.md                 ← this file (tracked)
├── linux-2019/               ← gitignored
└── windows-lenovo/           ← gitignored
    └── YOGA.BOOK.2.Eink.Reader_v1.0.0.5/
```

---

## Linux 2019 driver

**Source:** [aleksb/yogabook-c930-linux-eink-driver](https://github.com/aleksb/yogabook-c930-linux-eink-driver)

Community reverse-engineered USB driver (GPL v2). Last updated September 2019.

| What | Why (read only) |
|------|-----------------|
| `driver/eink.c` | Example sequences for init/kb/draw/xfer/blit — do not copy |
| `usb-protocol.md`, `drawing-images.md` | USB framing and xfer/blit semantics |
| `wireshark/protocol.lua` | USB dissector + `enable_kb.pcap` sample |

**Fetch:**

```bash
git clone --depth 1 https://github.com/aleksb/yogabook-c930-linux-eink-driver.git \
  /tmp/yogabook-linux-ref
cp -a /tmp/yogabook-linux-ref/. reference/linux-2019/
rm -rf /tmp/yogabook-linux-ref
```

Or use `scripts/fetch-references.sh`.

---

## Windows Lenovo open source

**Source:** Lenovo Yoga Book C930 open-source compliance package
(`YOGA.BOOK.2.Eink.Reader_v1.0.0.5`, GPLv3)

| Link | Description |
|------|-------------|
| [Direct CDN tarball](https://download.lenovo.com/consumer/mobiles/yoga.book.2.e-reader_v1.0.0.5.tar.gz) | `yoga.book.2.e-reader_v1.0.0.5.tar.gz` (~450 MB) |
| [Open Source Code — Yoga Book C930](https://pcsupport.lenovo.com/us/en/products/tablets/yoga-series/yoga-book-c930/downloads/ds503569) | Support page (same package) |

**SHA256:** `05ba9b512e54bc4a59335adec5aaf04dd4d9b2044b1fb7ad28a4d5a652e86ce9`

```bash
mkdir -p reference/windows-lenovo
curl -L -o reference/windows-lenovo/yoga.book.2.e-reader_v1.0.0.5.tar.gz \
  https://download.lenovo.com/consumer/mobiles/yoga.book.2.e-reader_v1.0.0.5.tar.gz
tar -xzf reference/windows-lenovo/yoga.book.2.e-reader_v1.0.0.5.tar.gz \
  -C reference/windows-lenovo
# → reference/windows-lenovo/YOGA.BOOK.2.Eink.Reader_v1.0.0.5/
```

**License:** GPLv3 (Lenovo headers). **Not in the archive:** `EinkSvr.exe`,
`itetcon.dll` (closed binaries).

### Key files (after extract)

| File | Why (read only) |
|------|-----------------|
| `inc/tconcmd.h` | USB opcode table |
| `inc/itetcon.h` | TCON API + `TRSP_SYSTEM_INFO_DATA` |
| `inc/EinkIteAPI.h` | App-facing API surface (inform D-Bus design) |
| `inc/SvrMsg.h` | Sleep/resume message IDs |
| `EinkIteAPI/USBHIDAPI.cpp` | Touch HID report layout |
| `comm/EiUpdate.cpp` | Partial-update algorithm reference — reimplement, don't port |

The full tree is large (~8500 files, bundled SumatraPDF/MuPDF). For driver work,
only `inc/`, `EinkIteAPI/`, and `comm/` matter.

---

## How agents / contributors should use references

1. Run `scripts/fetch-references.sh` before opcode or HID lookups.
2. **Implement clean-room in `kernel/` and `userspace/`** — never copy reference
   source into production trees.
3. Cross-check opcode tables (`tconcmd.h`) against hardware and Linux USB traces.
4. See [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) for layer rules and
   [docs/BLUEPRINT.md](../docs/BLUEPRINT.md) for roadmap.
