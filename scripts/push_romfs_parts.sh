#!/bin/bash
# Copy a game's RomFS image to its iPhone app in pieces: Documents/romfs.img.part000, .part001, ... and, once every piece is there, the count in Documents/romfs.img.parts.
# The app joins them into Documents/romfs.img at its next start (dm/ios/Sources/main.m romfs_join_parts). For images too large for one devicectl copy:
# Mario Kart World's 22 GB image broke off with "The connection was interrupted" every time, over Wi-Fi and USB. Each piece is retried on its own.
# Usage: push_romfs_parts.sh <device UDID> <bundle ID> <RomFS.img> [piece MB, default 1024] [first piece, default 0]
set -eu
[ "$#" -ge 3 ] || { echo "usage: $0 <device UDID> <bundle ID> <RomFS.img> [piece MB] [first piece]" >&2; exit 1; }
dev=$1 id=$2 img=$3 mb=${4:-1024} first=${5:-0}
size=$(stat -f %z "$img")
n=$(( (size + mb * 1048576 - 1) / (mb * 1048576) ))
echo "identity: $(python3 -I "$(dirname "$0")/../dm/tools/romfs_image.py" --identity "$img")"
echo "$size bytes in $n pieces of $mb MB"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/romfs-parts.XXXXXX")
trap 'rm -f "$tmp"/piece "$tmp"/count "$tmp"/err; rmdir "$tmp"' EXIT
for ((i = first; i < n; i++)); do
    name=$(printf 'romfs.img.part%03d' "$i")
    dd if="$img" of="$tmp/piece" bs=1m skip=$((i * mb)) count="$mb" 2>/dev/null
    for try in 1 2 3 4 5; do
        if xcrun devicectl device copy to --device "$dev" --domain-type appDataContainer --domain-identifier "$id" --source "$tmp/piece" --destination "Documents/$name" -q 2>"$tmp/err"; then break; fi
        echo "piece $i: try $try failed: $(grep -m1 -i error "$tmp/err" || true)"
        [ "$try" = 5 ] && { echo "[RED] piece $i not copied; rerun with first piece $i: $0 $dev $id $img $mb $i" >&2; exit 1; }
        sleep 3
    done
    echo "piece $((i + 1)) / $n"
done
echo "$n" > "$tmp/count"
xcrun devicectl device copy to --device "$dev" --domain-type appDataContainer --domain-identifier "$id" --source "$tmp/count" --destination Documents/romfs.img.parts -q
echo "game files on the phone in $n pieces: the app joins them at its next start (open it again if it says so)"
