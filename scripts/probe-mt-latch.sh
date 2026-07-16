#!/usr/bin/env bash
# Probe ITE8951 cold mt-arm via eink_drm sysfs (mt-enter + finger-enable).
# Usage: sudo ./scripts/probe-mt-latch.sh
set -euo pipefail

SYS=
for d in /sys/bus/usb/devices/*:1.0; do
	[[ -f "$d/scenario" && -f "$d/mt_latch" ]] || continue
	SYS=$d
	break
done

if [[ -z "$SYS" ]]; then
	for d in /sys/bus/usb/devices/*; do
		[[ -f "$d/scenario" && -f "$d/mt_latch" ]] || continue
		SYS=$d
		break
	done
fi

if [[ -z "$SYS" ]]; then
	echo "eink_drm sysfs not found (load eink_drm.ko first)" >&2
	exit 1
fi

echo "sysfs: $SYS"
echo -n "scenario (before): "
cat "$SYS/scenario"

echo "echo 1 > mt_latch  (cold mt-arm) ..."
echo 1 > "$SYS/mt_latch"

echo -n "mt_latch (before after): "
cat "$SYS/mt_latch"
echo -n "scenario (after): "
AFTER=$(cat "$SYS/scenario")
echo "$AFTER"

if [[ "$AFTER" != "3" ]]; then
	echo "NOT MT yet: GET=$AFTER (want 3). Check: dmesg | grep -i 'MT arm'"
	exit 2
fi

echo "PASS: GET=3 after mt-arm"
echo "Check dmesg for 'finger bit' / display_cfg …28… or …|0x00080000"
echo "Next: two-finger on E-Ink; expect hidraw report id 0x0c (not only 0x90):"
echo "  sudo ./userspace/eink-touchpad/eink-touchpad -l"
echo "  sudo xxd /dev/hidrawN   # look for 0c .. count>=2"
exit 0
