# RoughStomp

A guitar rig plugin (VST3, AU, Standalone) built with JUCE: pre-amp pedals ->
two NAM amp captures side by side (Amp B optional), each into its own cab IR and
panned -> FX loop. It's a companion to RoughTrack
(github.com/dantaipan/roughtrack), where it's used as an insert.
README.md covers using and building it; PLAN.md is the roadmap and history.

## Layout

- `fx-core/`: JUCE-free DSP (pedals, resampler, tuner pitch detection) with
  Catch2 tests, including regression renders (`fx-core/tests/regression`)
- `ui/`: shared GUI. `rs_ui/Family.h` is the look shared with RoughTrack;
  `rs_ui/Pedal.h` is the pedals' own look
- `plugins/Rig` (RoughStomp) and `plugins/Overdrive`
- `tools/`: `rs-render` (a pedal over a WAV), `rs-host-render` (a built plugin
  over a WAV, through its saved state), `rs-rig-snapshot` (a picture of the
  rig's editor), `validate.sh` (pluginval)

## The family look (shared with RoughTrack)

`ui/include/rs_ui/Family.h` and `ui/src/Family.cpp` are the one copy of the
look both apps use: colours, buttons, menus, console knobs, fader caps.
RoughTrack fetches this repo at a pinned commit (`cmake/Dependencies.cmake`
there) and compiles `Family.cpp` into its app. After changing either file:
commit and push here, then bump the pin in RoughTrack and check its
`--snapshot` pictures. Pedals keep their own hardware look (`Pedal.h`).

## Working here

- Build with the CMake presets: `cmake --preset release`,
  `cmake --build --preset release`. Use Release for anything you listen to:
  Debug drops out, and a Debug build also overwrites the installed plugins
  (COPY_PLUGIN_AFTER_BUILD)
- Before finishing a change: `build/release/fx-core/fx-core-tests`,
  `tools/validate.sh release 10`, and for GUI changes an `rs-rig-snapshot`
  picture
- A deliberate change to a pedal's sound needs its regression references
  rewritten (`RS_UPDATE_REGRESSION=1 ... "[regression]"`) and reviewed
- README screenshots live in `docs/images` and are made with
  `rs-rig-snapshot --scale=2` (the tuner one with `--tone=HZ`); load captures
  and IRs under neutral names (e.g. "Clean Amp", "Open-Back 2x12"), not the
  commercial ones, and set pedal menus to sensible values
- `tools/make_release.sh` makes the notarized download (the `dist` preset:
  universal, Developer ID); it uploads to Apple's notary service, so only run
  it when asked
- TONE3000 (`tone3000/`, `rs::t3k`; the rig's browser is
  `plugins/Rig/Source/Tone3000Browser`): the publishable key lives in
  `tone3000/client_id.local` (git-ignored; never commit it). The session is
  `~/Library/Application Support/RoughStomp/TONE3000/session.json`, shared by
  every process; refresh tokens rotate, so renewing re-reads it under the
  lock. Ask for A2 models first (the API hides them otherwise). TONE3000's
  terms: download only what the user asks for, show "Powered by TONE3000",
  the full logo before the T3K mark. `rs-t3k` tries the API from the command
  line; `rs-rig-snapshot --tone3000=amp[:trending|:<toneId>] --wait=6`
  pictures the browser (`pre2`, `loop1`... for a capture slot)
- Pedal slots can hold a NAM capture (`plugins/Rig/Source/CapturePedal`, id
  `nam`, not in fx-core's registry); `Rig.pre1=nam Rig.pre1File=...` sets one
  up in `rs-host-render`/`rs-rig-snapshot`. The slider view is a global
  preference (`ViewSettings`); `rs-rig-snapshot --view=sliders` pictures it
  without changing the saved preference
- Branding is "RoughStomp" (manufacturer code Rstm, bundle IDs com.roughstomp.*)
- fx-core and the NAM library are built with hidden symbols so the plugin can
  load inside RoughTrack, which has its own copy of NAM; keep it that way
