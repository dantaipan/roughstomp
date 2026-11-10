#!/bin/bash
# Makes RoughStomp's macOS icon (resources/icon/AppIcon.iconset, every size
# iconutil expects) from its source, resources/icon/roughstomp-icon.svg (a
# 1024 x 1024 canvas). Rendered with macOS's Quick Look, which handles the
# SVG's clip path and blurred shadow; the PNGs are committed, so builds don't
# need this.
set -euo pipefail
cd "$(dirname "$0")/.."

svg="resources/icon/roughstomp-icon.svg"
set_dir="resources/icon/AppIcon.iconset"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

qlmanage -t -s 1024 -o "$work" "$svg" >/dev/null 2>&1
master="$work/$(basename "$svg").png"
[ -s "$master" ] || { echo "Quick Look couldn't render $svg" >&2; exit 1; }

mkdir -p "$set_dir"
for spec in 16:icon_16x16 32:icon_16x16@2x 32:icon_32x32 64:icon_32x32@2x \
            128:icon_128x128 256:icon_128x128@2x 256:icon_256x256 512:icon_256x256@2x \
            512:icon_512x512 1024:icon_512x512@2x; do
    size=${spec%%:*}
    name=${spec#*:}
    if [ "$size" = 1024 ]; then
        cp "$master" "$set_dir/$name.png"
    else
        sips -z "$size" "$size" "$master" --out "$set_dir/$name.png" >/dev/null
    fi
done
echo "Wrote $set_dir from $svg"
