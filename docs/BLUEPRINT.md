# YogaBook C930 E-Ink — Project Blueprint

**Device:** Lenovo YogaBook C930 (YB-J912) — **not** the Lenovo Yoga C930 laptop.

**Goal:** Make the E-Ink panel a first-class Linux display with usable keyboard,
text-optimised workflows, and room for custom layouts, reader, and draw modes.

---

## Executive summary

The YogaBook C930 E-Ink panel is a **USB composite device** (`048d:8951`) with
three logical subsystems on separate interfaces:

| Subsystem | USB interface | Role |
|-----------|---------------|------|
| ITE8951 display controller | Vendor bulk (`0xFF`) | Init, waveforms, pixel upload, blit |
| Touch controller | HID (`0x90` touch, `0x91` pen) | Finger/stylus input |
| Keyboard firmware | HID keyboard | Touch→keycode in keyboard mode (pre-OS capable) |

**We are not porting reference code.** The 2019 Linux driver and Lenovo Windows
tree inform **what the hardware expects** (opcodes, structs, sequences). All
production code is a clean-room Linux implementation: DRM/KMS kernel driver,
userspace daemon, standard APIs.

See [ARCHITECTURE.md](ARCHITECTURE.md) for layer rules and reference policy.

---

## What we know

### Validated on hardware (2026-07-11)

| Item | Status |
|------|--------|
| USB `048d:8951` — vendor bulk + 3× HID | ✅ |
| Clean-room GET_SYS doorknock ×4 | ✅ |
| USBC/USBS framing, 31-byte control packet | ✅ |
| Draw mode + keyboard exit sequence | ✅ |
| Center-patch draw (200×200 grey ramp) | ✅ |
| **First pixel on DRM connector** | ✅ |
| **Live compositor output (niri → USB-1)** | ✅ — terminal spawned on E-Ink |
| Reference driver smoke test (`test-board.sh`) | ✅ — independent hardware check |

**Validated environment:** Fedora 44, kernel `7.1.3-200.fc44.x86_64`, compositor
**niri**, module `eink_drm.ko` built out-of-tree.

### Validated in `eink_drm` (production code)

- USB protocol: GET_SYS, SET_WAVEFORM, GET_DPY_STATUS, SCENARIO, WRITE_REG,
  LD_IMG (`0xA8`), BLIT (`0x94`)
- DRM/KMS: connector, 1920×1080 mode, dumb buffer, atomic commit via
  `drm_simple_display_pipe`
- Compositor path: imported dma-buf (`drm_gem_fb_begin_cpu_access` / `vmap`)
- Full-frame upload in 1920×32 chunks (≤61440 B per xfer)
- Module params: `drm_enable`, `drm_stage`, `panel_test_pattern`

### Partially known

- GET_SYS reports `image_buf` / `update_buf` addresses (e.g. `0x08353800`) —
  **not used for xfer/blit**; reference uses fixed `0x00382f30`
- Sleep/resume orchestration (message IDs in `SvrMsg.h`, logic in closed `EinkSvr`)
- Keyboard layout blob format (`kb_blob` in `tconcmd.h`)
- Full register map (two addresses used: `0x18001224`, `0x18001138`)
- Optimal waveform policy (currently blit payload uses `0xff` / CURRENT)

### Closed / missing

- `EinkSvr.exe` source (init state machine, reconnect, app lifecycle)
- `itetcon.dll` implementation (byte-level sequences for some `Tcon*` calls)
- Pre-OS keyboard layout is firmware — custom layouts need post-boot emulation

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Apps: terminal, neovim, reader, draw, compositor (niri)      │
├─────────────────────────────────────────────────────────────┤
│  einkd — mode orchestrator (D-Bus)                          │
│    • keyboard | reader | draw | display | off               │
│    • owns HID touch fd, keyboard emulator plugin            │
│    • does NOT own pixels (DRM driver does)                    │
├─────────────────────────────────────────────────────────────┤
│  DRM/KMS driver (eink_drm.ko) — FIRST-CLASS DISPLAY         │
│    • connector on card0 (niri: "USB-1" @ 1920×1080)          │
│    • atomic commits from compositor / fbcon                 │
│    • internal: waveform pick, blit, engine wait               │
│    • (future) diff, ghosting budget, periodic GC16 cleanse  │
├─────────────────────────────────────────────────────────────┤
│  USB 048d:8951                                              │
│    vendor bulk (display)  +  HID (touch/pen/keyboard)       │
└─────────────────────────────────────────────────────────────┘
```

### Design principles

1. **First-class display** — kernel DRM connector, not `/dev/eink0` text hacks.
2. **E-Ink smarts in the driver** — diff, waveforms, ghosting live in DRM commits.
3. **Modes in userspace** — `einkd` switches keyboard/reader/draw; doesn't render.
4. **Text-native UI** — no cursor blink, pagination over scroll, convergent navigation.
5. **Custom keyboards via emulation** — touch HID + layout map + uinput (post-boot).
6. **Clean-room implementation** — reference archives inform hardware calls only;
   no porting `eink.c` or Windows sources into production code.
7. **Don't replicate Windows** — no D2D, no SumatraPDF fork, no 80-message IPC bus.

### E-Ink UI rules (text workflows)

- No cursor blink; static bar or blind mode
- Pagination / half-screen scroll, not line-scroll
- DU2 for keystroke feedback; GL16 for text; GC16 cleanse every N partials
- Convergent navigation (search, paragraph jump) over arrow-key loops
- Optional: typewriter mode, explicit WYSIWYG refresh on demand

See [Kragen Sitaker's e-ink editor design notes](https://dercuano.github.io/notes/eink-design.html).

---

## Hardware bring-up (verified commands)

### Clean start (preferred)

`niri msg action quit` does not reliably release the module on this machine.
**Reboot** is the cleanest reset (clears USB ghost outputs, keyboard/draw state,
and compositor DRM holds).

```bash
# After reboot:
cd ~/yogabook-c930-linux-eink-driver/kernel/eink_drm
make

sudo rmmod eink_drm 2>/dev/null || true

# Center-patch smoke test (200×200 grey ramp, once per boot):
sudo insmod ./eink_drm.ko drm_enable=1 drm_stage=5 panel_test_pattern=1

# Compositor / daily use:
sudo insmod ./eink_drm.ko drm_enable=1 drm_stage=5 panel_test_pattern=0
```

Log in, start niri, then:

```bash
niri msg action load-config-file
niri msg outputs          # expect ONE "USB-1" with Current mode 1920x1080
niri msg action focus-monitor-right
niri msg action spawn -- ghostty
```

### Watch kernel logs

```bash
journalctl -k -b | grep -E 'xfer_base|center patch|mem_addr|draw mode|fb sample|blit|failed'
# live:
journalctl -k -f | grep -E 'xfer_base|center patch|mem_addr|draw mode|fb sample|blit|failed'
```

**Good log lines (center patch):**

```
panel init complete (4 doorknocks), xfer_base=0x00382f30 (GET_SYS image=0x08353800 ...)
draw mode active (display_cfg was 0x...)
center patch 200x200 at (860,440) xfer_base=0x00382f30 (off=0)
blit mem_addr=0x0029fb94 wf=255 at (860,440) 200x200
center patch blit done (waveform 255) — wait ~30s
```

**Good log lines (compositor):**

```
fb sample pixels: offset=... pitch=... center=0x00262626 origin=0x...
blit mem_addr=0x00382f30 wf=255 at (0,0) 1920x1080
frame blit 1920x1080 complete (waveform 255)
```

`mem_addr=0xfc2d34a4` means **wrong addressing** (regression) — must be
`0x0029fb94` for center patch or `0x00382f30` for full-frame blit at origin.

### Module parameters

| Param | Default | Purpose |
|-------|---------|---------|
| `drm_enable=1` | 0 | Register DRM device + connector |
| `drm_stage=5` | 0 | KMS init depth (1–5 bisect); **5 = full pipe** |
| `panel_test_pattern=1` | 1 | Upload 200×200 center grey ramp instead of compositor FB |

### niri output config

`~/.config/niri/config.kdl` — place E-Ink to the right of internal panel:

```kdl
output "USB-1" {
    mode "1920x1080@60.000"
    scale 1.0
    position x=1707 y=0
}
```

After multiple `rmmod`/`insmod` without reboot, connector names can become
`USB-2`, `USB-3`, … while config still targets `USB-1`. **Fix: reboot**, then
**one** `insmod`.

### Reload without reboot (if session quit works)

```bash
sudo loginctl terminate-user "$USER"
sleep 3
sudo rmmod eink_drm
sudo insmod ~/yogabook-c930-linux-eink-driver/kernel/eink_drm/eink_drm.ko \
    drm_enable=1 drm_stage=5 panel_test_pattern=0
```

### Reference driver sanity check (not production)

```bash
cd ~/yogabook-c930-linux-eink-driver
sudo ./scripts/test-board.sh --yes --all
```

Uses `reference/linux-2019/driver/eink.ko` and `/dev/eink0` text protocol.
Confirms hardware independent of DRM. **Never load `eink.ko` and `eink_drm.ko`
together.**

### Keyboard recovery

```bash
sudo ~/yogabook-c930-linux-eink-driver/reference/linux-2019/enable-eink-kb.sh
```

Or reboot.

---

## Roadmap

**Last updated:** 2026-07-11 · **Hardware validated on:** YogaBook C930, Fedora 44,
kernel `7.1.3-200.fc44.x86_64`, niri compositor.

### Phase 0 — Foundation ✅

- [x] Reference layout, fetch script, docs, Cursor rules
- [x] Hardware validated: USB `048d:8951`, vendor bulk + 3× HID
- [x] Bring-up script (`scripts/test-board.sh`)

### Phase 1 — Display driver core ✅

**Milestone:** Panel shows pixels reliably via DRM. **Achieved 2026-07-11.**

| Task | Status |
|------|--------|
| Clean-room ITE8951 USB protocol | **Done** |
| USB probe (vendor bulk `0xFF` only) | **Done** |
| Register DRM device, connector, 1920×1080 mode | **Done** |
| Dumb buffer + atomic commit (`drm_simple_display_pipe`) | **Done** |
| Draw mode + keyboard exit + pixel upload + blit | **Done** |
| Compositor dma-buf import (niri) | **Done** |
| Display-ready wait (`0xB1`) | **Done** |
| Waveform in blit payload (`0xff` CURRENT) | **Done** |
| Suspend/resume | Not started |
| udev / module autoload for `048d:8951` | Not started |
| Stable connector name (`Eink-1`) | Not started |

**Test:** `insmod eink_drm.ko drm_enable=1 drm_stage=5 panel_test_pattern=0` →
niri shows `USB-1` @ 1920×1080; terminal renders on E-Ink.

### Phase 2 — Smart refresh (next)

**Milestone:** Partial updates without accumulating ghosting.

- [ ] Shadow framebuffer in driver; diff on commit
- [ ] Tile-based dirty region merge (algorithm informed by `EiUpdate.cpp`, not ported)
- [ ] Per-region waveform selection (INIT first frame, GC16/GL16/DU2 thereafter)
- [ ] Ghosting budget → automatic GC16 cleanse
- [ ] DRM connector properties: `WAVEFORM`, `FORCE_CLEANSE`
- [ ] Cap compositor refresh rate / coalesce commits

**Test:** Small region updates repeatedly; ghost clears on schedule.

### Phase 3 — Input daemon (`einkd`)

**Milestone:** Stock QWERTY keyboard works; sleep recovery works.

- [ ] Claim HID touch interface (`0x90`/`0x91`)
- [ ] Keyboard mode: firmware KB **or** draw mode + layout emulator
- [ ] Coordinate with DRM driver (keyboard exit before compositor draw)
- [ ] `uinput` virtual keyboard
- [ ] D-Bus API: `org.yogabook.Eink` — SetMode, Status
- [ ] systemd: boot load, sleep hook (`REOPEN_8951` logic)
- [ ] Coordinate transform (touch 7680×4320 → display 1920×1080)

**Test:** Type in terminal on E-Ink output; close lid and recover.

### Phase 4 — Custom keyboard layouts

**Milestone:** Numpad-only layout works.

- [ ] Layout definition format (JSON: key rects → keycodes)
- [ ] Layout renderer via DRM commit
- [ ] Touch→key mapper in `einkd`
- [ ] Layout switcher (D-Bus / CLI)

### Phase 5 — Text terminal on E-Ink

**Milestone:** Terminal + neovim usable on E-Ink DRM output.

- [x] Terminal on E-Ink DRM connector (ghostty via niri) — **initial validation**
- [ ] DECSET 2026 batching, no blink cursor
- [ ] Optional `eink.nvim`: pagination, blind, typewriter modes
- [ ] LSP + basic plugins

### Phase 6 — Reader & draw plugins

- [ ] Reader: PDF via poppler/mupdf, page-turn gestures
- [ ] Draw: pen HID or hardware handwriting (`0xAC`/`0xAE`)

### Phase 7 — Compositor integration

- [x] niri output config for `USB-1` — **initial validation**
- [ ] Stable naming / udev for connector ID
- [ ] max_render_time / refresh cap
- [ ] Dual-screen: main LCD + E-Ink reference pane

---

## Progress log (2026-07-11)

### Milestone: first-class display output

1. DRM registration on hardware — single `USB-1` connector after reboot + one
   `insmod` (GUD-aligned init order).
2. Draw mode enters; compositor pixels reach driver (`fb sample pixels:
   center=0x00262626`).
3. Center-patch test visible — 200×200 grey ramp at screen centre.
4. Full compositor path — niri dma-buf → chunked upload → blit → **terminal on
   E-Ink**.

### Key files (production code)

```
kernel/eink_drm/
  eink_drm_drv.c            USB probe, reconnect reset, module params
  eink_panel.h              1920×1080, USB IDs
  protocol/ite8951.h        Opcodes, structs, API
  protocol/ite8951_usb.c    USB transport, draw mode, load, blit
  drm/eink_drm_drv.h        Device state, update_lock
  drm/eink_drm_kms.c        Connector, pipe, fb import, patch test
  Makefile
  README.md
scripts/test-board.sh       Reference-driver hardware smoke test
```

### Lessons learned

#### USB transport (do not regress)

| Finding | Detail |
|---------|--------|
| **`usb_bulk_msg()` fails on OUT** | Returns `-EAGAIN`. Use **async URBs** for OUT. |
| **Persistent bulk IN URB** | One `bulk_in_urb` + 4 KB buffer at probe; reuse every IN. |
| **IN before OUT for responses** | Post bulk IN URB **before** USBC control packet. |
| **IN buffer size** | Always `ITE8951_IN_BUFFER_SIZE` (4096) for IN URBs. |
| **Response + status in one read** | GET_SYS: 112 B response + 13 B USBS often in one transfer. |
| **OUT transfer length** | `build_ctrl_packet` length = payload size for write_reg, load, blit. |
| **Autosuspend** | `usb_disable_autosuspend()` before bulk I/O. |
| **Drain timeout** | ~100 ms for pipe drain, not 5 s command timeout. |
| **Stale state on reconnect** | Probe/disconnect reset `usb_link`, destroy mutex, re-init flags. |
| **Conflicting modules** | Never load `eink.ko` (reference) and `eink_drm.ko` together. |
| **Fedora logging** | Use `journalctl -k`; `dmesg` may need root. |

#### Pixel path (validated on hardware)

| Finding | Detail |
|---------|--------|
| **Xfer/blit base address** | Always **`0x00382f30`** for LD_IMG and BLIT mem_addr — same as
  reference `eink.c`. GET_SYS may report `image_buf=0x08353800`; **do not use**
  that for addressing. |
| **Blit mem_addr formula** | `0x00382f30 + off - x - 1920*y`. Center patch (off=0,
  x=860, y=440): **`0x0029fb94`**. Full frame (off=0, x=0, y=0): **`0x00382f30`**. |
| **Panel stride** | Always **1920** in blit formula. Do not use GET_SYS width if
  unexpected (mis-parse produced `mem_addr=0xfc2d34a4` → white screen). |
| **Blit waveform** | Payload waveform **`0xff`** (CURRENT) — reference hardcodes this.
  INIT/GC16 in blit did not produce visible refresh in testing. |
| **Pixel format** | 1 byte/pixel: `(grey << 4) \| 0x0f`, grey 0–15. |
| **Xfer limit** | Max **61440** bytes (1920×32) per LD_IMG. |
| **Keyboard exit** | `ite8951_exit_keyboard_mode()` before draw: waveform `0x200`,
  scenario `0x00040000`, poll panel mode `0x18001224` == `0x80000000`. |
| **Draw mode** | Scenarios `0x01010000` / `0x00000000`; write display cfg
  `0x002e0000` to `0x18001138`. |
| **Compositor FB** | Imported dma-bufs: `drm_gem_fb_begin_cpu_access`, `vmap`,
  include `fb->offsets[0]`; skip blit on upload failure. |
| **Concurrent updates** | `mutex_trylock` on `update_lock` — drop duplicate commits. |
| **E-Ink refresh latency** | Allow **~30 s** after first blit before judging result. |
| **USB ghost outputs** | Repeated load/unload creates `USB-2`, `USB-3` in niri.
  Reboot + single `insmod` → only `USB-1`. |

#### Center-patch test (reference-aligned)

Matches `scripts/test-board.sh` / `eink.c` text protocol:

```
xfer 0 200 200     → load at buffer offset 0
<40000 bytes>      → grey ramp rows
blit 0 860 440 200 200
```

`panel_test_pattern=1` runs this once per boot inside `eink_pipe_update`.

### Protocol framing (clean-room, validated)

- **USBC** control: 31 bytes, SCSI opcode `0xFE`, ITE opcode in CDB.
- **GET_SYS** (`0x80`): signature `0x38393531`, 112-byte `TRSP_SYSTEM_INFO_DATA`
  layout per `itetcon.h` (width @+16, height @+20, image buf @+28).
- **LD_IMG** (`0xA8`): host → panel RAM at `0x00382f30 + offset`.
- **BLIT** (`0x94`): `TDRAW_UPD_ARG_DATA` — mem_addr, waveform, x, y, w, h.
- Three-step exchange: control OUT → data IN/OUT → status IN.

---

## Agent handoff — do this next

**Goal:** Production-ready daily driver (Phase 2–3).

**Prerequisites:** Hardware path proven; read lessons above before changing
protocol or addressing.

**Priority order:**

1. **Coordinator input scenario** — Windows leave-KB is proven
   ([PROTOCOL_WINDOWS.md](PROTOCOL_WINDOWS.md)): `0xA6` address `0x03000000`,
   GET→`0`. Port that encoding into `ite8951_usb.c` (do not require GET=`3`).
2. **Packaging / boot** — `modprobe.d`, udev softdep, optional autoload on
   `048d:8951`; stable connector naming in DRM.
3. **Refresh policy** — shadow FB, dirty rects, waveform selection, ghosting
   cleanse; cap niri commit rate.
4. **`einkd` skeleton** — D-Bus mode API; coordinate keyboard exit with DRM;
   sleep/resume re-init from `SvrMsg.h` message IDs.
5. **Suspend/resume** — full USB reconnect + panel re-init sequence.

**Do not:**

- Use GET_SYS `image_buf` for xfer/blit addresses (use `0x00382f30`)
- Change blit stride away from 1920 without hardware retest
- Port or copy from `reference/linux-2019/driver/eink.c`
- Put pixel/upload logic in userspace
- Break persistent IN URB / async OUT transport

**Read first:** [ARCHITECTURE.md](ARCHITECTURE.md), [CODING_STYLE.md](CODING_STYLE.md),
`kernel/eink_drm/protocol/ite8951_usb.c`, `kernel/eink_drm/drm/eink_drm_kms.c`.

---

## Repository layout

```
/
├── README.md
├── docs/
│   ├── BLUEPRINT.md          ← roadmap + bring-up log (this file)
│   ├── ARCHITECTURE.md
│   ├── CODING_STYLE.md
│   ├── PROTOCOL_WINDOWS.md   ← validated leave-KB / 0xA6 wire notes
│   └── WINDOWS_CAPTURE.md    ← dual-boot USB RE checklist
├── kernel/
│   └── eink_drm/             ← active driver (USB + DRM)
├── userspace/                ← einkd (Phase 3, not started)
├── scripts/
│   ├── fetch-references.sh
│   └── test-board.sh         ← reference-driver smoke test only
├── reference/                ← gitignored hardware docs
└── .cursor/rules/
```

---

## Key reference files (after local fetch)

Run `./scripts/fetch-references.sh` first. See [reference/README.md](../reference/README.md).

| Topic | Location (local, gitignored) |
|-------|------------------------------|
| Linux USB driver (2019) | `reference/linux-2019/driver/eink.c` |
| USB protocol notes | `reference/linux-2019/usb-protocol.md` |
| Xfer/blit semantics | `reference/linux-2019/drawing-images.md` |
| GET_SYS struct layout | `reference/windows-lenovo/.../inc/itetcon.h` |
| Windows opcode table | `reference/windows-lenovo/.../inc/tconcmd.h` |
| Touch HID parser | `reference/windows-lenovo/.../EinkIteAPI/USBHIDAPI.cpp` |
| Diff update logic | `reference/windows-lenovo/.../comm/EiUpdate.cpp` |

---

## Risks

| Risk | Mitigation |
|------|------------|
| Wrong xfer/blit addressing | Fixed base `0x00382f30`; log `mem_addr` on every blit |
| USB transport quirks | Documented in Progress log; persistent IN URB + async OUT |
| Ghost USB outputs in compositor | Reboot between experiments; single `insmod` |
| `usbhid` / module hold | Reboot or `loginctl terminate-user` before `rmmod` |
| Sleep still broken | Phase 3: full re-init from `SvrMsg.h` |
| Secure Boot | Sign module or disable SB |
| GUI on E-Ink | Text-native workflows; compositor for layout only |

---

## Success criteria

| Stage | Done when | Status |
|-------|-----------|--------|
| **First pixel** | Visible pattern on panel via DRM | ✅ 2026-07-11 |
| **Compositor output** | App/window on E-Ink connector | ✅ niri + ghostty |
| **Usable daily** | Type on keyboard, survive sleep, auto-start on boot | — |
| **Better than Windows** | Custom layouts, cleaner refresh policy | — |
| **Developer tool** | Neovim + LSP on E-Ink DRM output | — |
| **First-class citizen** | Stable connector name; fbcon works | partial (`USB-1`) |
