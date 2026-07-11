#!/usr/bin/env bash
# Safe progressive hardware test for the YogaBook C930 (YB-J912) E-Ink panel.
#
# Uses the REFERENCE 2019 module (reference/linux-2019/) for bring-up only —
# not part of the production eink_drm stack. See docs/ARCHITECTURE.md.
#
# Default behaviour is read-only: USB presence, interface layout, driver state.
# Driver load and pixel tests require explicit flags and root.
#
# Usage:
#   ./scripts/test-board.sh                 # read-only checks (safe)
#   ./scripts/test-board.sh --load          # build/load 2019 driver, verify probe
#   ./scripts/test-board.sh --draw          # visible center patch (needs loaded driver)
#   ./scripts/test-board.sh --all           # --load then --draw
#   ./scripts/test-board.sh --yes --all     # skip confirmation prompts
#
# Have SSH available before loading the driver (2019 driver can wedge the panel).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DRIVER_DIR="$ROOT/reference/linux-2019/driver"
RECOVERY_SCRIPT="$ROOT/reference/linux-2019/enable-eink-kb.sh"

USB_VID="048d"
USB_PID="8951"
PANEL_W=1920
PANEL_H=1080
MAX_XFER_BYTES=61440

DO_LOAD=0
DO_DRAW=0
ASSUME_YES=0
TIMEOUT_WRITE=30

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { printf "${GREEN}PASS${NC}  %s\n" "$*"; }
fail() { printf "${RED}FAIL${NC}  %s\n" "$*"; FAILED=1; }
warn() { printf "${YELLOW}WARN${NC}  %s\n" "$*"; }
info() { printf "      %s\n" "$*"; }

FAILED=0

usage() {
	sed -n '3,14p' "$0" | sed 's/^# \{0,1\}//'
	exit "${1:-0}"
}

confirm() {
	local prompt="$1"
	if [[ "$ASSUME_YES" -eq 1 ]]; then
		return 0
	fi
	printf "${YELLOW}??${NC}  %s [y/N] " "$prompt"
	read -r reply
	[[ "$reply" =~ ^[Yy]$ ]]
}

need_root() {
	if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
		fail "This step needs root. Re-run with: sudo $0 $*"
		exit 1
	fi
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		-h|--help) usage 0 ;;
		--load) DO_LOAD=1 ;;
		--draw) DO_DRAW=1 ;;
		--all) DO_LOAD=1; DO_DRAW=1 ;;
		--yes|-y) ASSUME_YES=1 ;;
		*) echo "Unknown option: $1" >&2; usage 1 ;;
	esac
	shift
done

if [[ "$DO_DRAW" -eq 1 && "$DO_LOAD" -eq 0 ]]; then
	# draw still needs a loaded driver; allow if already present
	:
fi

echo "YogaBook C930 E-Ink board test"
echo "=============================="
info "Repo: $ROOT"
echo

# ---------------------------------------------------------------------------
# Phase 0 — read-only USB / hardware presence
# ---------------------------------------------------------------------------
echo "Phase 0: USB hardware (read-only)"
echo "---------------------------------"

if lsusb -d "${USB_VID}:${USB_PID}" >/dev/null 2>&1; then
	pass "USB device ${USB_VID}:${USB_PID} present"
	lsusb -d "${USB_VID}:${USB_PID}"
else
	fail "USB device ${USB_VID}:${USB_PID} not found"
	info "Is this a YogaBook C930 (YB-J912), not the Yoga C930 laptop?"
	echo
	exit 1
fi

if command -v lsusb >/dev/null; then
	vendor_bulk=0
	hid_ifaces=0
	while IFS= read -r line; do
		case "$line" in
			*"bInterfaceClass"*"255"*) vendor_bulk=1 ;;
			*"bInterfaceClass"*"3 "*) hid_ifaces=$((hid_ifaces + 1)) ;;
		esac
	done < <(lsusb -v -d "${USB_VID}:${USB_PID}" 2>/dev/null | grep -E 'bInterfaceClass|iInterface' || true)

	if [[ "$vendor_bulk" -eq 1 ]]; then
		pass "Vendor bulk interface (ITE8951 display) visible"
	else
		warn "Could not confirm vendor bulk interface — lsusb -v may need root"
	fi
	if [[ "$hid_ifaces" -ge 1 ]]; then
		pass "HID interface(s) present ($hid_ifaces)"
	else
		warn "No HID interfaces seen in lsusb -v output"
	fi
fi

echo

# ---------------------------------------------------------------------------
# Phase 1 — driver / device node state (read-only)
# ---------------------------------------------------------------------------
echo "Phase 1: Driver state (read-only)"
echo "---------------------------------"

if lsmod | awk '{print $1}' | grep -qx eink; then
	pass "eink kernel module loaded"
else
	info "eink module not loaded (expected before first test)"
fi

EINK_DEV=""
for dev in /dev/eink*; do
	[[ -e "$dev" ]] || continue
	EINK_DEV="$dev"
	break
done

if [[ -n "$EINK_DEV" ]]; then
	pass "Character device present: $EINK_DEV"
else
	info "/dev/eink* not present — load the 2019 driver with --load"
fi

if [[ -d "$DRIVER_DIR" ]]; then
	pass "2019 reference driver source: $DRIVER_DIR"
	if [[ -f "$DRIVER_DIR/eink.ko" ]]; then
		pass "Built module found: $DRIVER_DIR/eink.ko"
	else
		info "No eink.ko yet — --load will try to build it"
	fi
else
	warn "2019 driver missing. Fetch with: ./scripts/fetch-references.sh --linux"
fi

if dmesg 2>/dev/null | grep -qiE 'eink|8951'; then
	info "Recent kernel messages mentioning eink/8951:"
	dmesg 2>/dev/null | grep -iE 'eink|8951' | tail -5 | sed 's/^/        /'
fi

echo

# ---------------------------------------------------------------------------
# Phase 2 — optional driver load (moderate risk)
# ---------------------------------------------------------------------------
if [[ "$DO_LOAD" -eq 1 ]]; then
	echo "Phase 2: Load 2019 driver"
	echo "-------------------------"
	need_root "$*"

	if [[ ! -d "$DRIVER_DIR" ]]; then
		fail "Driver source missing at $DRIVER_DIR"
		exit 1
	fi

	if ! confirm "Unload usbhid/wacom and load experimental eink.ko?"; then
		info "Skipped driver load."
		DO_DRAW=0
	else
		if [[ ! -f "$DRIVER_DIR/eink.ko" ]]; then
			info "Building eink.ko ..."
			if [[ ! -d "/lib/modules/$(uname -r)/build" ]]; then
				fail "Kernel headers missing for $(uname -r)"
				info "On Fedora/RHEL:  sudo dnf install kernel-devel-$(uname -r) gcc make"
				info "On Debian/Ubuntu: sudo apt install linux-headers-$(uname -r) build-essential"
				info "Then reboot only if dnf/apt installed a newer kernel — headers must match uname -r."
				exit 1
			fi
			make -C "$DRIVER_DIR" >/dev/null
		fi

		info "Unloading conflicting modules ..."
		rmmod wacom 2>/dev/null || true
		rmmod usbhid 2>/dev/null || true
		rmmod eink 2>/dev/null || true

		info "Loading eink.ko ..."
		if insmod "$DRIVER_DIR/eink.ko"; then
			pass "insmod eink.ko"
		else
			fail "insmod eink.ko"
			exit 1
		fi

		sleep 1
		modprobe usbhid 2>/dev/null || true
		modprobe wacom 2>/dev/null || true

		EINK_DEV=""
		for dev in /dev/eink*; do
			[[ -e "$dev" ]] || continue
			EINK_DEV="$dev"
			break
		done

		if [[ -n "$EINK_DEV" ]]; then
			pass "Device node: $EINK_DEV"
		else
			fail "/dev/eink* did not appear after insmod"
			info "Check: dmesg | tail -30"
			DO_DRAW=0
		fi

		if dmesg 2>/dev/null | tail -20 | grep -qi 'eink'; then
			pass "Probe messages in dmesg"
			dmesg 2>/dev/null | grep -i eink | tail -5 | sed 's/^/        /'
		else
			warn "No recent eink lines in dmesg"
		fi

		info "Driver auto-runs init + keyboard enable on probe."
		info "Try typing on the E-Ink keyboard before any draw test."
	fi
	echo
fi

# ---------------------------------------------------------------------------
# Phase 3 — optional draw test (single xfer, restores keyboard)
# ---------------------------------------------------------------------------
if [[ "$DO_DRAW" -eq 1 ]]; then
	echo "Phase 3: Draw test"
	echo "------------------"
	need_root "$*"

	if [[ -z "$EINK_DEV" ]]; then
		for dev in /dev/eink*; do
			[[ -e "$dev" ]] || continue
			EINK_DEV="$dev"
			break
		done
	fi

	if [[ -z "$EINK_DEV" ]]; then
		fail "No /dev/eink* — run with --load first"
		exit 1
	fi

	# Centered patch, offset 0 only (documented safe). One xfer, well under 61440 bytes.
	PATCH_W=200
	PATCH_H=200
	PATCH_X=$(( (PANEL_W - PATCH_W) / 2 ))
	PATCH_Y=$(( (PANEL_H - PATCH_H) / 2 ))
	PATCH_BYTES=$((PATCH_W * PATCH_H))

	if (( PATCH_BYTES > MAX_XFER_BYTES )); then
		fail "Internal error: patch exceeds xfer limit"
		exit 1
	fi

	if ! confirm "Draw ${PATCH_W}x${PATCH_H} test pattern at screen centre (pauses before restoring keyboard)?"; then
		info "Skipped draw test."
	else
		info "Switching to draw mode and drawing ..."
		draw_rc=0
		python3 - "$EINK_DEV" "$PATCH_W" "$PATCH_H" "$PATCH_X" "$PATCH_Y" <<'PY' || draw_rc=$?
import sys
import time

dev_path = sys.argv[1]
w, h = int(sys.argv[2]), int(sys.argv[3])
x, y = int(sys.argv[4]), int(sys.argv[5])

# Grey ramp bars — partial E-Ink updates show smooth gradients better than
# a 2-colour checkerboard (which often looks uniformly white).
def make_pixels(width, height):
    out = bytearray()
    bands = 16
    for row in range(height):
        grey = min(15, (row * bands) // height)
        byte = (grey << 4) | 0x0F
        out.extend([byte] * width)
    return bytes(out)

pixels = make_pixels(w, h)

with open(dev_path, "wb", buffering=0) as dev:
    dev.write(b"draw\n")
    time.sleep(0.5)
    dev.write(f"xfer 0 {w} {h}\n".encode())
    dev.write(pixels)
    dev.write(f"blit 0 {x} {y} {w} {h}\n".encode())

print(f"Pattern sent: {w}x{h} grey ramp at ({x},{y})", file=sys.stderr)
PY

		if [[ "$draw_rc" -eq 0 ]]; then
			pass "xfer + blit completed"
			info "You should see a square with horizontal grey bands (dark at top → light at bottom)."
			if [[ "$ASSUME_YES" -eq 0 ]]; then
				printf "${YELLOW}??${NC}  Press Enter to restore keyboard mode ... "
				read -r _
			else
				info "Waiting 5s before restoring keyboard (--yes) ..."
				sleep 5
			fi
		else
			fail "draw pattern failed (exit $draw_rc)"
		fi

		info "Restoring keyboard mode ..."
		if timeout "$TIMEOUT_WRITE" bash -c "printf 'kb\n' > '$EINK_DEV'"; then
			pass "keyboard mode restored"
		else
			fail "kb command failed — run: sudo $RECOVERY_SCRIPT"
		fi
	fi
	echo
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo "Summary"
echo "-------"
if [[ "$FAILED" -eq 0 ]]; then
	pass "All executed checks passed."
else
	fail "Some checks failed."
fi

echo
info "Recovery after sleep or stuck keyboard:"
info "  sudo $RECOVERY_SCRIPT"
info "Read-only re-check anytime:"
info "  $0"

exit "$FAILED"
