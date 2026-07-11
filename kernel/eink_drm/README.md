# eink_drm — Lenovo YogaBook C930 E-Ink DRM driver

**Status:** Phase 1 — USB probe + ITE8951 doorknock **working on hardware** (2026-07-11).
DRM connector registration is next. See [docs/BLUEPRINT.md](../../docs/BLUEPRINT.md)
(agent handoff section).

## Build

```bash
cd kernel/eink_drm
make
```

Requires `kernel-devel-$(uname -r)`.

## Load (development)

`eink_drm.ko` and reference `eink.ko` both bind the vendor bulk interface — load
**one only**.

**Recommended** (paths work from anywhere):

```bash
cd ~/yogabook-c930-linux-eink-driver
sudo ./scripts/test-eink-drm.sh --usb-only       # step 1: USB only
DRM_STAGE=1 sudo ./scripts/test-eink-drm.sh --build --yes   # DRM bisect
cat logs/eink-drm/latest.log
```

Manual bisect from `kernel/eink_drm/` (note `./eink_drm.ko`, not `kernel/...`):

```bash
cd ~/yogabook-c930-linux-eink-driver/kernel/eink_drm
make

sudo rmmod eink_drm 2>/dev/null
# Optional panel USB reset (skip if reference driver not built):
# sudo insmod ../../reference/linux-2019/driver/eink.ko && sudo rmmod eink
sleep 1

sudo insmod ./eink_drm.ko drm_enable=1 drm_stage=1
journalctl -k -n 20 --no-pager | rg 'eink_drm|DRM stage|panel init'
```

Then increase `drm_stage` (2, 3, 4, 5) after each success. Reference reset
requires `./scripts/fetch-references.sh` and building `reference/linux-2019/driver/`.

Success:

```
YogaBook E-Ink panel USB link ready
panel init complete (4 doorknocks)
```

## Layout

```
eink_drm_drv.c           USB probe, autosuspend, module entry
eink_panel.h             1920×1080, USB IDs
protocol/ite8951.h       Opcodes, struct ite8951_usb
protocol/ite8951_usb.c   Async OUT + persistent IN URB; USBC/USBS exchange
drm/                     (next) DRM connector + commit path
```

## USB transport notes

Do not use `usb_bulk_msg()` — bulk OUT returns `-EAGAIN` on this panel.
See **Lessons learned** in [docs/BLUEPRINT.md](../../docs/BLUEPRINT.md).

Policy: [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md) ·
Style: [docs/CODING_STYLE.md](../../docs/CODING_STYLE.md)
