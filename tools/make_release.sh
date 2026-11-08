#!/bin/bash
# Builds RoughStomp for distribution: the standalone app and the VST3 and AU
# plugins, universal, signed with Developer ID, notarized and stapled, inside a
# signed, notarized and stapled disk image in dist/. (Modelled on RoughTrack's
# tools/make_release.sh.)
#
# Needs a "Developer ID Application" certificate in the keychain and notarytool
# credentials saved under a keychain profile (the same Apple account as
# RoughTrack's works):
#   xcrun notarytool store-credentials roughtrack-notary --apple-id ... --team-id ...

set -euo pipefail

# RoughTrack's profile; credentials saved before its rename are under rifftrack-notary
profile="${NOTARY_PROFILE:-roughtrack-notary}"
if [ -z "${NOTARY_PROFILE:-}" ] && ! xcrun notarytool history --keychain-profile "$profile" >/dev/null 2>&1; then
    profile="rifftrack-notary"
fi
identity="Developer ID Application"

cd "$(dirname "$0")/.."

cmake --preset dist
cmake --build --preset dist --target Rig_Standalone Rig_VST3 Rig_AU

artefacts="build/dist/plugins/Rig/Rig_artefacts/Release"
app="$artefacts/Standalone/RoughStomp.app"
vst3="$artefacts/VST3/RoughStomp.vst3"
au="$artefacts/AU/RoughStomp.component"
version=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app/Contents/Info.plist")
out="dist"
work="$out/work"

rm -rf "$work"
mkdir -p "$work"

echo "Checking RoughStomp $version"
for bundle in "$app" "$vst3" "$au"; do
    codesign --verify --strict --deep --verbose=1 "$bundle"
    binary="$bundle/Contents/MacOS/RoughStomp"
    lipo -archs "$binary"
    # Hidden symbols keep the plugin's NAM apart from RoughTrack's (see CLAUDE.md)
    weak=$(nm -gUm "$binary" | grep -c "weak external" || true)
    if [ "$weak" != "0" ]; then
        echo "$bundle exports $weak weak symbols: it would clash with RoughTrack's NAM" >&2
        exit 1
    fi
done

echo "Notarizing the app and plugins"
mkdir "$work/bundles"
ditto "$app" "$work/bundles/RoughStomp.app"
ditto "$vst3" "$work/bundles/RoughStomp.vst3"
ditto "$au" "$work/bundles/RoughStomp.component"
ditto -c -k "$work/bundles" "$work/RoughStomp.zip"
xcrun notarytool submit "$work/RoughStomp.zip" --keychain-profile "$profile" --wait
for bundle in "$app" "$vst3" "$au"; do
    xcrun stapler staple "$bundle"
done

echo "Making the disk image"
image="$work/image"
mkdir -p "$image/Plugins"
ditto "$app" "$image/RoughStomp.app"
ln -s /Applications "$image/Applications"
ditto "$vst3" "$image/Plugins/RoughStomp.vst3"
ditto "$au" "$image/Plugins/RoughStomp.component"
ln -s /Library/Audio/Plug-Ins/VST3 "$image/Plugins/VST3 folder"
ln -s /Library/Audio/Plug-Ins/Components "$image/Plugins/Components folder"
cat > "$image/Plugins/Install Plugins.txt" <<'TEXT'
Installing the RoughStomp plugins
=================================

RoughStomp.app runs on its own. To use RoughStomp inside RoughTrack, Logic
or another DAW, install the plugins too:

- Drag RoughStomp.vst3 onto "VST3 folder"
- Drag RoughStomp.component onto "Components folder" (Audio Unit, for Logic
  and GarageBand)

Those folders are for every user on this Mac, so macOS asks for your
password. To install just for yourself instead, copy them to the same
folders inside your own Library:

  ~/Library/Audio/Plug-Ins/VST3
  ~/Library/Audio/Plug-Ins/Components

Then rescan plugins in your DAW (in RoughTrack: rescan plugins, then add
RoughStomp as an insert).
TEXT

dmg="$out/RoughStomp-$version.dmg"
rm -f "$dmg"
hdiutil create -volname "RoughStomp $version" -srcfolder "$image" -fs HFS+ -format UDZO "$dmg"
codesign --sign "$identity" --timestamp "$dmg"

echo "Notarizing the disk image"
xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait
xcrun stapler staple "$dmg"

echo "Checking what a downloaded copy will see"
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
spctl --assess --type execute --verbose=2 "$app"
xcrun stapler validate "$dmg"
xcrun stapler validate "$vst3"
xcrun stapler validate "$au"

rm -rf "$work"
echo "Done: $dmg"
