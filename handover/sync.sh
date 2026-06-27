#!/usr/bin/env bash
# Refresh handover/ from docs/firmware, win-captures, and reference symlink.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HO="$ROOT/handover"

rm -rf "$HO/firmware"
cp -a "$ROOT/docs/firmware" "$HO/firmware"

rm -rf "$HO/win-captures"
cp -a "$ROOT/win-captures" "$HO/win-captures"

rm -f "$HO/windows-lenovo"
ln -sfn "$ROOT/reference/yoga.book.2.e-reader_v1.0.0.5" "$HO/windows-lenovo"

echo "handover/ refreshed:"
echo "  firmware/       ($(find "$HO/firmware" -type f | wc -l) files)"
echo "  win-captures/   ($(find "$HO/win-captures" -type f | wc -l) files)"
if [[ -L "$HO/windows-lenovo" && -d "$HO/windows-lenovo" ]]; then
	echo "  windows-lenovo/ -> $(readlink -f "$HO/windows-lenovo")"
else
	echo "  windows-lenovo/ MISSING — run: ./scripts/fetch-references.sh"
fi
