#!/usr/bin/env bash
# Fetch reference archives into reference/ (gitignored).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LINUX_REF="$ROOT/reference/linux-2019"
WINDOWS_REF="$ROOT/reference/windows-lenovo"
LINUX_UPSTREAM="https://github.com/aleksb/yogabook-c930-linux-eink-driver.git"

usage() {
	echo "Usage: $0 [--linux] [--windows] [--all]"
	echo "  --linux    Clone 2019 community driver into reference/linux-2019/"
	echo "  --windows  Print instructions for Lenovo Windows source (manual download)"
	echo "  --all      Linux fetch + Windows instructions (default)"
}

fetch_linux() {
	if [[ -d "$LINUX_REF/driver" ]]; then
		echo "reference/linux-2019/ already exists — remove it to re-fetch."
		return 0
	fi
	echo "Fetching Linux 2019 reference from $LINUX_UPSTREAM ..."
	tmp="$(mktemp -d)"
	git clone --depth 1 "$LINUX_UPSTREAM" "$tmp"
	mkdir -p "$LINUX_REF"
	cp -a "$tmp/." "$LINUX_REF/"
	rm -rf "$tmp"
	echo "Done: $LINUX_REF"
}

fetch_windows_instructions() {
	cat <<EOF

Windows reference (manual download — not automated):

  1. Open Lenovo support:
     https://pcsupport.lenovo.com/us/en/products/tablets/yoga-series/yoga-book-c930/downloads/ds503569

  2. Download the "Open Source Code" package for Yoga Book C930.

  3. Extract and place the tree here:
     $WINDOWS_REF/YOGA.BOOK.2.Eink.Reader_v1.0.0.5/

  Key headers: inc/tconcmd.h, inc/itetcon.h, inc/EinkIteAPI.h, inc/SvrMsg.h

EOF
}

do_linux=false
do_windows=false

if [[ $# -eq 0 ]]; then
	do_linux=true
	do_windows=true
else
	for arg in "$@"; do
		case "$arg" in
			--linux) do_linux=true ;;
			--windows) do_windows=true ;;
			--all) do_linux=true; do_windows=true ;;
			-h|--help) usage; exit 0 ;;
			*) echo "Unknown option: $arg"; usage; exit 1 ;;
		esac
	done
fi

$do_linux && fetch_linux
$do_windows && fetch_windows_instructions
