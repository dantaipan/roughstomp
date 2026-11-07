#!/usr/bin/env bash
# Run pluginval against the built VST3 (and AU on macOS).
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
ARTEFACTS="$ROOT/build/$PRESET/plugins/Overdrive/Overdrive_artefacts/$CONFIG"

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

VST3="$ARTEFACTS/VST3/RoughStomp Overdrive.vst3"
if [ -d "$VST3" ]; then
  echo "== Validating VST3 (strictness $STRICTNESS) =="
  "$PLUGINVAL" --strictness-level "$STRICTNESS" --validate-in-process --validate "$VST3" || status=1
else
  echo "VST3 not found at: $VST3 (build first)"; status=1
fi

if [[ "$(uname)" == "Darwin" ]]; then
  AU="$HOME/Library/Audio/Plug-Ins/Components/RoughStomp Overdrive.component"
  if [ -d "$AU" ]; then
    echo "== Validating AU (strictness $STRICTNESS) =="
    "$PLUGINVAL" --strictness-level "$STRICTNESS" --validate-in-process --validate "$AU" || status=1
  fi
fi

exit $status
