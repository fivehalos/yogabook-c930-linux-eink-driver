#!/usr/bin/env bash
# Load eink_drm on hardware and capture a full test log.
#
# Usage:
#   ./scripts/test-eink-drm.sh --usb-only       # USB init only (drm_enable=0)
#   ./scripts/test-eink-drm.sh --build          # DRM registration (drm_enable=1)
#   ./scripts/test-eink-drm.sh --build --yes    # skip confirmation
#
# Logs: logs/eink-drm/<timestamp>.log and logs/eink-drm/latest.log

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODULE_DIR="$ROOT/kernel/eink_drm"
REF_KO="$ROOT/reference/linux-2019/driver/eink.ko"
LOG_DIR="$ROOT/logs/eink-drm"

DRM_ENABLE=1
ASSUME_YES=0

usage() {
	sed -n '3,8p' "$0" | sed 's/^# \{0,1\}//'
	exit "${1:-0}"
}

confirm() {
	local prompt="$1"
	if [[ "$ASSUME_YES" -eq 1 ]]; then
		return 0
	fi
	printf '??  %s [y/N] ' "$prompt"
	read -r reply
	[[ "$reply" =~ ^[Yy]$ ]]
}

log() {
	printf '%s\n' "$*" | tee -a "$LOGFILE"
}

run() {
	log "+ $*"
	"$@" 2>&1 | tee -a "$LOGFILE"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--usb-only) DRM_ENABLE=0 ;;
	--build) DRM_ENABLE=1 ;;
	--yes) ASSUME_YES=1 ;;
	-h|--help) usage 0 ;;
	*) echo "unknown option: $1" >&2; usage 1 ;;
	esac
	shift
done

if [[ "$(id -u)" -ne 0 ]]; then
	echo "run as root (sudo ./scripts/test-eink-drm.sh ...)" >&2
	exit 1
fi

mkdir -p "$LOG_DIR"
LOGFILE="$LOG_DIR/$(date +%Y%m%d-%H%M%S).log"
ln -sf "$(basename "$LOGFILE")" "$LOG_DIR/latest.log"

log "=== eink_drm hardware test ==="
log "time: $(date -Is)"
log "host: $(hostname)"
log "kernel: $(uname -r)"
log "drm_enable: $DRM_ENABLE"
log "drm_stage: ${DRM_STAGE:-5}"
log "logfile: $LOGFILE"
log ""

if [[ ! -s "$MODULE_DIR/eink_drm.ko" ]]; then
	log "ERROR: $MODULE_DIR/eink_drm.ko missing or empty — run 'make' in kernel/eink_drm first"
	exit 1
fi

log "--- module info ---"
modinfo "$MODULE_DIR/eink_drm.ko" | tee -a "$LOGFILE"
log ""

if [[ "$DRM_ENABLE" -eq 1 ]]; then
	confirm "Load eink_drm with DRM enabled? (can panic during bring-up)" || exit 0
else
	confirm "Load eink_drm USB-only (drm_enable=0)?" || exit 0
fi

log "--- reset panel USB via reference driver ---"
run rmmod eink_drm 2>/dev/null || true
if [[ -f "$REF_KO" ]]; then
	run insmod "$REF_KO"
	run rmmod eink
	sleep 1
else
	log "WARN: $REF_KO not found — skipping reference reset"
fi

log "--- load eink_drm.ko drm_enable=$DRM_ENABLE drm_stage=${DRM_STAGE:-5} ---"
run insmod "$MODULE_DIR/eink_drm.ko" "drm_enable=$DRM_ENABLE" "drm_stage=${DRM_STAGE:-5}"
sleep 1

log "--- kernel log (eink_drm / DRM / panic) ---"
journalctl -k -n 80 --no-pager 2>&1 | rg -i 'eink_drm|eink\.ko|panel init|DRM:|panic|oops|BUG:|RIP:|Call Trace|drm_WARN' | tee -a "$LOGFILE" || true

if [[ -r /sys/fs/pstore/console-ramoops-0 ]]; then
	log "--- pstore console (panic trace) ---"
	tail -80 /sys/fs/pstore/console-ramoops-0 2>&1 | tee -a "$LOGFILE" || true
fi

log ""
log "=== done ==="
log "Full log: $LOGFILE"
log "Latest:   $LOG_DIR/latest.log"
