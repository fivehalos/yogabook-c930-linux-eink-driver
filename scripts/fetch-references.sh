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

WINDOWS_TARBALL_URL="https://download.lenovo.com/consumer/mobiles/yoga.book.2.e-reader_v1.0.0.5.tar.gz"
WINDOWS_TARBALL_SHA256="05ba9b512e54bc4a59335adec5aaf04dd4d9b2044b1fb7ad28a4d5a652e86ce9"

fetch_windows_instructions() {
	cat <<EOF

Windows reference (Lenovo OSS tarball — gitignored under reference/windows-lenovo/):

  Direct download:
    $WINDOWS_TARBALL_URL
  SHA256: $WINDOWS_TARBALL_SHA256

  mkdir -p "$WINDOWS_REF"
  curl -L -o "$WINDOWS_REF/yoga.book.2.e-reader_v1.0.0.5.tar.gz" \\
    "$WINDOWS_TARBALL_URL"
  tar -xzf "$WINDOWS_REF/yoga.book.2.e-reader_v1.0.0.5.tar.gz" -C "$WINDOWS_REF"
  # → $WINDOWS_REF/YOGA.BOOK.2.Eink.Reader_v1.0.0.5/

  Key headers: inc/tconcmd.h, inc/itetcon.h, inc/EinkIteAPI.h, inc/SvrMsg.h
  Support page (same package):
    https://pcsupport.lenovo.com/us/en/products/tablets/yoga-series/yoga-book-c930/downloads/ds503569

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
