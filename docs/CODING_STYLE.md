# Coding style — C throughout

All production code in this repository is **C**: kernel driver, `einkd`, libraries,
and tools. Same readability goals everywhere — Linux kernel norms in the driver,
idiomatic POSIX/userspace C in daemons.

Policy context: [ARCHITECTURE.md](ARCHITECTURE.md) · Roadmap: [BLUEPRINT.md](BLUEPRINT.md)

---

## Goals (in order)

1. **Correct** — matches kernel APIs, checkpatch-clean where practical
2. **Readable** — names and structure carry the story
3. **Minimal** — no cleverness, no noise comments, no dead code

---

## Kernel (`kernel/eink_drm/`)

Follow upstream norms unless this section says otherwise:

- [Linux kernel coding style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
- [Submitting patches](https://www.kernel.org/doc/html/latest/process/submitting-patches.html) (format, sign-off)
- Run `scripts/checkpatch.pl` on touched files when available
- SPDX licence identifier in every source file (`GPL-2.0-only` for the driver)
- Use `pr_fmt`, `dev_dbg`, `dev_err` — not `printk` with hard-coded prefixes
- Prefer `goto` cleanup labels over deep nesting (one label per function is fine)

---

## Names should teach the reader

Choose names a newcomer can parse without opening a datasheet.

| Avoid | Prefer | Why |
|-------|--------|-----|
| `pkt`, `buf`, `rc` alone | `usb_response`, `pixel_buffer`, `ret` | Abbreviations hide intent |
| `x`, `y`, `w`, `h` in protocol code | `dest_x`, `region_width` | Screen coords vs buffer offsets differ |
| `send_ctrl(dev, 4, 0xa8, …)` | `ite8951_load_image_area(…)` | Opcodes belong behind named wrappers |
| `st`, `tmp`, `data` | `probe_stage`, `bulk_out_buffer` | Generic names force comment debt |

**Hardware constants** — use `#define` or `enum` with a short comment only if the
name alone is not enough:

```c
/* USB vendor command: load host pixels into panel RAM (ITE8951 opcode 0xA8) */
#define ITE8951_CMD_LOAD_IMAGE_AREA  0xA8
```

Register addresses: name the *purpose*, not just the hex:

```c
#define ITE8951_REG_PANEL_MODE  0x18001224u
```

---

## Structure: one idea per block

Break functions into visible phases. Use a blank line between logical steps.
Section comments are allowed for long functions — keep them short:

```c
int eink_panel_blit_rect(struct eink_panel *panel,
			 const struct eink_rect *src,
			 const struct eink_rect *dest)
{
	int ret;

	/* Wait until the display engine accepts another update */
	ret = ite8951_wait_display_ready(panel->usb, PANEL_READY_TIMEOUT_MS);
	if (ret)
		return ret;

	ret = ite8951_send_blit(panel->usb, src, dest);
	if (ret)
		return ret;

	return ite8951_wait_display_ready(panel->usb, PANEL_READY_TIMEOUT_MS);
}
```

Early returns for errors; avoid `if/else` ladders when a straight line reads better.

---

## Comments: concise, only when needed

**Do not** narrate obvious code:

```c
/* Bad: increment i */
i++;
```

**Do** explain non-obvious protocol, hardware, or trade-offs:

```c
/*
 * Panel RAM stores each row as width bytes followed by padding to 1920
 * columns. We upload one tight rectangle at a time so regions do not overlap.
 */
```

**Do** document invariants at struct or file scope when the type is shared:

```c
/**
 * struct eink_panel - YogaBook C930 E-Ink panel state
 * @width: Visible horizontal resolution (1920)
 * @shadow_fb: CPU copy of last full frame for diffing on commit
 *
 * The USB device is claimed on the vendor bulk interface only; HID is left
 * to userspace (einkd).
 */
struct eink_panel {
	...
};
```

Use kernel-doc (`/** ... */`) on exported symbols. Internal `static` helpers need
kernel-doc only when the behaviour is not obvious from the name.

---

## Wrappers over raw numbers

Hide USB packet layout and opcodes behind small, named functions. The call site
should read like a checklist:

```c
/* Good — readable call site */
ret = ite8951_doorknock(panel->usb);
if (ret)
	return ret;

ret = ite8951_set_waveform(panel->usb, EINK_WAVEFORM_GL16);
```

```c
/* Bad — reader must know opcode table */
ret = ite8951_usb_xfer(panel->usb, 0x80, 0x38393531, ...);
```

One level of naming is enough. Do not build abstraction frameworks.

---

## Types and headers

- `kernel/eink_drm/protocol/` — USB / ITE8951 only
- `kernel/eink_drm/drm/` — DRM types only; include protocol via narrow headers
- Shared panel dimensions and greyscale format in one header (e.g. `eink_panel.h`)
- No includes from `reference/` trees

---

## Error handling and logging

```c
ret = ite8951_load_pixels(usb, &region, pixel_data, pixel_count);
if (ret) {
	dev_err(dev, "failed to upload %dx%d region at (%d,%d): %d\n",
		region.width, region.height, region.x, region.y, ret);
	return ret;
}
```

Log messages: **what failed**, **with what inputs**, **return code**. Not a stack trace.

---

## What we avoid (even if reference code did it)

- Mega-functions mixing USB parsing, state machine, and sysfs/debug ioctls
- Cryptic single-letter locals outside tiny loop indices
- Commented-out experiments left in tree
- Copy-paste from `reference/linux-2019/` or Windows sources
- `/dev/eink0` string protocol in production code

---

## Userspace (`userspace/`)

Same clarity bar as the kernel: names teach, short functions, comments only where
the code is not self-explanatory.

**Baseline:**

- C11 (`-std=c11`), compiler warnings on (`-Wall -Wextra -Werror` in CI when ready)
- SPDX `GPL-2.0-only` (or `GPL-2.0-or-later` where linking requires it)
- No C++ in this repository
- Prefer POSIX + common Linux APIs: `sd-bus` (systemd) for D-Bus, `libevdev` /
  `hidraw` for input, `syslog` or `sd_journal` for logging
- `errno` + negative return codes, or project-local `enum eink_error` with a
  single `eink_strerror()` — pick one style per component and stick to it

**Naming** — same rules as kernel: `touch_coord_display_x`, not `tx`; `dbus_set_mode`,
not `set_mode` in a 4000-line file.

**Structure** — one job per `.c` file where practical:

```
userspace/einkd/
  main.c           # startup, signal handling
  dbus_api.c       # org.yogabook.Eink
  mode_keyboard.c  # keyboard mode logic
  hid_touch.c      # touch HID claim + coord transform
```

**Logging:**

```c
syslog(LOG_ERR, "failed to open hidraw %s: %s", path, strerror(errno));
```

**Comments** — document D-Bus contracts and coordinate transforms, not `fclose(f)`:

```c
/*
 * Touch reports native 7680x4320; panel is 1920x1080. Scale and clamp before
 * hit-testing layout keys.
 */
```

**What we avoid:**

- GObject boilerplate unless we explicitly choose it later (default: plain C)
- Mega `main()` with all logic inline
- Duplicating USB/panel protocol in userspace — pixels go through DRM only

**Optional editor plugins** (e.g. Neovim config) may use other languages; the
driver stack in `kernel/` and `userspace/` stays C.

---

## Review checklist

### Kernel C

Before submitting kernel C:

- [ ] Names explain roles without reading the whole file
- [ ] Complex hardware steps have a one-line comment or kernel-doc
- [ ] No reference-source copy-paste
- [ ] `checkpatch.pl` run on new files (or manual pass for obvious issues)
- [ ] Exported symbols documented; internal helpers kept short

### Userspace C

- [ ] Same naming and structure rules as kernel
- [ ] No panel USB protocol in userspace — DRM + D-Bus only
- [ ] Errors logged with context (path, errno, mode name)
- [ ] Build with warnings enabled; no unchecked `malloc` results
