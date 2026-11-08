# RoughStomp

A guitar rig plugin (VST3, AU, Standalone) built with JUCE: pre-amp pedals ->
NAM amp capture -> FX loop -> optional power amp -> cab IR. It's a companion
to RoughTrack (github.com/dantaipan/roughtrack), where it's used as an insert.
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
- Branding is "RoughStomp" (manufacturer code Rstm, bundle IDs com.roughstomp.*)
- fx-core and the NAM library are built with hidden symbols so the plugin can
  load inside RoughTrack, which has its own copy of NAM; keep it that way
