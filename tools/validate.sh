#!/usr/bin/env bash
# Run pluginval against every built plugin: VST3, and the installed AU on macOS.
# Usage: tools/validate.sh [debug|release] [strictness]
set -euo pipefail

PRESET="${1:-debug}"
STRICTNESS="${2:-5}"
case "$PRESET" in
  debug)   CONFIG=Debug ;;
  release) CONFIG=Release ;;
  *) echo "Unknown preset: $PRESET (use debug or release)"; exit 1 ;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Find pluginval: PATH first, then the usual macOS install location
if command -v pluginval >/dev/null 2>&1; then
  PLUGINVAL="pluginval"
elif [ -x "/Applications/pluginval.app/Contents/MacOS/pluginval" ]; then
  PLUGINVAL="/Applications/pluginval.app/Contents/MacOS/pluginval"
else
  echo "pluginval not found. Download it from https://github.com/Tracktion/pluginval/releases"
  exit 1
fi

status=0

# Target name and product name of every plugin
PLUGINS=("Overdrive:RoughStomp Overdrive" "Rig:RoughStomp")

for entry in "${PLUGINS[@]}"; do
  TARGET="${entry%%:*}"; PRODUCT="${entry#*:}"
  ARTEFACTS="$ROOT/build/$PRESET/plugins/$TARGET/${TARGET}_artefacts/$CONFIG"

  VST3="$ARTEFACTS/VST3/$PRODUCT.vst3"
  if [ -d "$VST3" ]; then
    echo "== $PRODUCT: VST3 (strictness $STRICTNESS) =="
    "$PLUGINVAL" --strictness-level "$STRICTNESS" --validate-in-process --validate "$VST3" || status=1
  else
    echo "VST3 not found at: $VST3 (build first)"; status=1
  fi

  if [[ "$(uname)" == "Darwin" ]]; then
    AU="$HOME/Library/Audio/Plug-Ins/Components/$PRODUCT.component"
    if [ -d "$AU" ]; then
      echo "== $PRODUCT: AU (strictness $STRICTNESS) =="
      "$PLUGINVAL" --strictness-level "$STRICTNESS" --validate-in-process --validate "$AU" || status=1
    fi
  fi
done

exit $status
