# RoughStomp — Development Plan

A complete guitar rig built with JUCE: pre-amp pedals, one or two NAM amp captures each into its own cabinet IR, and post FX, in one plugin (VST3/AU) and a standalone app for macOS. Load it in RoughTrack (or any DAW) the way you would AmpliTube; for a single pedal, fill one slot and leave the amps and cabs empty. The DSP lives in a JUCE-free library, `fx-core`.

```
guitar -> input, gate -> [ pre-amp pedals ] -┬-> AMP A (NAM) -> CAB A (IR) -> pan -┬-> [ post FX ] -> out
                         drive, wah, octave  └-> AMP B (NAM) -> CAB B (IR) -> pan -┘   EQ, modulation,
                                                 empty by default  "same as A" empty    delay, reverb
```

**Post FX** was the FX loop until 0.6.2, and its code, parameter IDs and state keys still say `loop` (`loop1`, `loopSend`, `Section::loop`); sessions, presets and automation depend on them. A real loop sits between preamp and power amp, but most NAM captures are a whole amp, so these effects come after the amps and cabs, as studio effects do. Until 0.6 a power-amp slot sat after the loop; sessions that used it get its capture in a Post FX slot.

---

## Guiding principles

- **One DSP core, many front-ends.** Every effect is a plain C++ class with no JUCE dependencies. The plugin and RoughTrack both use it.
- **Real-time safe audio thread.** No allocation, locks, file I/O or logging in `process()`. Allocate in `prepare()`.
- **Smooth everything.** Every user parameter goes through a smoother to avoid zipper noise.
- **Test by ear and by numbers.** Unit tests for DSP math, regression renders for every pedal, pluginval for hosts, blind A/B for tone.
- **Ship small.** Each step ends with something that runs in a DAW.

## Conventions

- C++20, `float` audio path, `double` for LFO/phase accumulators
- Effects never touch JUCE GUI or `AudioProcessor`; the plugin owns parameters and pushes values to the effect each block
- Parameter IDs and state keys are stable once released (sessions, presets and automation depend on them); new parameters default to what the rig did before they existed
- Every effect gets: unit tests, a regression render, a pluginval pass

---

## Phase 8 — Toward 1.0

**Goal:** shape a capture's tone without spending a pedal slot, play live from a MIDI controller, and know the rig holds up in real hosts. Then 1.0.

### 8a. Tone around the captures

A NAM capture has no tone controls of its own, so today shaping one costs an EQ pedal.

- [x] Tone stack per amp: Bass, Middle, Treble after the capture (`fx::ToneStack` in `fx/ToneShaping.h`: low shelf 120 Hz, mid peak 650 Hz, high shelf 2.8 kHz, +-12 dB). Flat, it stands aside, so existing sessions render bit-identical to 0.6.2 (checked against a 0.6.2 build: amp, full rig, both amps panned)
- [x] Low cut (20–500 Hz) and high cut (1.5–20 kHz) per cab, after the IR, 12 dB/octave (`fx::CutFilters`); fully down / fully up is off; switching fades
- [x] A *Tone* button on each amp strip (lit while anything isn't flat) opens a call-out: Bass / Middle / Treble | Low cut / High cut, Reset. Parameters `ampBass`, `ampMiddle`, `ampTreble`, `ampBBass`..., `cabLowCut`, `cabHighCut`, `cabBLowCut`, `cabBHighCut` (version hint 2), flat/off by default. Factory presets keep them, with the amp and cab
- [x] Tests (`[tone]` in fx-core-tests), pluginval, `rs-rig-snapshot --amp-tone=A`, README
- Lesson: a host's "0 dB" can come back from its normalised form as 2e-7 (`-12 + 0.1 * 120` in float), which kept the "flat" stack running and moved the output by a float step. Anything that stands aside at a value needs a tolerance

### 8b. MIDI control

Live use, mostly in the standalone app, where nothing else can switch presets or footswitches.

- [ ] Take MIDI in: the VST3 gets an event input, the AU stays an effect (`aufx`), the standalone app offers MIDI inputs in Options
- [ ] Program change selects a preset, in the preset menu's order
- [ ] MIDI Learn on knobs, footswitches and switches. Mappings are a global preference, like the slider view, since your controller is the same in every session. CC reaches the audio thread's parameters without locks, so an expression pedal on the Wah stays smooth
- [ ] Tests for the mapping and program change; README

### 8c. Before 1.0

- [ ] CPU budget: measure the worst case (two amps, NAM captures in Post FX, which run once per side) at 44.1 and 48 kHz, small buffers; say in the README what to expect
- [ ] Host pass: auval, Logic, Live, Reaper, RoughTrack Swift
- [ ] 1.0 release

## Later ideas

- CLAP format (clap-juce-extensions)
- Two IRs blended in one cab (a mic blend)
- Scenes within a preset (footswitch states and a few knobs, switched without reloading)
- Circuit-modelled drive stage (wave digital filters or nodal analysis), as a learning exercise; waveshaping held up in blind A/B, so not for tone's sake
- Windows build

---

## Shipped

Full checklists are in git history (PLAN.md before 2026-11-20).

- **Phases 0–1 (2026-11-07):** CMake/JUCE 9.0.4 scaffold, pluginval script, the `fx::Effect` interface, smoothers, a JUCE-free polyphase oversampler, filters, waveshapers, Catch2 tests, `rs-render`
- **Phases 2–3 (2026-11-07/08):** Overdrive v1 (8x oversampled, auto makeup, clean blend, click-free bypass) and v2 (soft / hard / asymmetric modes, touch-sensitive bias); the shared pedal look (`rs_ui/Pedal.h`)
- **Phase 4 (2026-11-08):** the rig plugin: pedal registry, allocation-free chain swaps, 6 + 6 slots, NAM amp, cab IR, the resampler (`fx::RateBridge`), session state, `rs-host-render`
- **Phase 5 (2026-11-08):** LFO (tempo sync, phase-locked to the host's bar), delay line; EQ, Chorus, Delay (digital, tape echo, ping-pong), Reverb (FDN room / hall / plate), Spring, Tremolo, Vibrato, Flanger, Phaser
- **Phase 6 (2026-11-08):** rig presets, drag to reorder, strobe tuner, noise gate, Compressor, Wah, the polished rig GUI
- **Phase 7 (2026-11-08 to 11-14):**
  - regression renders for every pedal; click-free amp/cab switching; background capture loading
  - the family look shared with RoughTrack (`rs_ui/Family.h`)
  - more drives (Fuzz, Big Fuzz, Clean Drive, Blues Drive); the Octave pedal (Vintage divider, Poly filter bank)
  - notarized download (`tools/make_release.sh`) and app icon
  - TONE3000 browsing and loading, the sign-in shared with RoughTrack Swift
  - the slider view; NAM captures in pedal slots
  - the dual-amp rig (Amp B replaced the power amp); "Post FX"; the separate Overdrive plugin dropped (0.6.2)

## Findings worth keeping

- **Tone stacks and A/B:** a TS-style 700 Hz shelf on the Overdrive lost a blind A/B 0–3 (too much fizz above 3 kHz); the 600 Hz–8 kHz swept lowpass stayed. With it restored, v2 won 3–1 (Hard beat v1 at medium and high drive)
- **Resampling:** JUCE's own resampler let through highs a cab should cut (up to 60 dB too much above 16 kHz), so captures and IRs both go through `fx::RateBridge` (windowed sinc, fixed whole-sample latency: 66 samples at 44.1 kHz)
- **Spring reverb:** rebuilt after a blind A/B on the stretched-allpass model (Välimäki–Parker–Abel): three springs mostly summed to mono, 100 allpasses each, a 4.4 kHz cut-off, so echoes chirp and stay dark
- **Octave pedal:** test on fast passages, not only sustained notes: per-band "is this a note" decisions flickered and dropped notes, and a slow low-end envelope smeared fast lines into a drone. Measured by tracking a take's pitch and checking each 10 ms frame for the octave (both modes: no dropouts, Poly 0% and Vintage under 1% out of tune on the fast passage, 4–10 ms to respond)
- **TONE3000 API** (spike in `tools/t3k`, `rs-t3k`):
  - access tokens last 3600 s and the refresh token rotates on every refresh, so processes sharing a session re-read it under a lock before refreshing
  - without `architecture` A2 models are hidden, and nearly every current capture is A2-only: ask for A2, fall back to unset (A1 + custom) when there are none; IRs ignore it
  - the lists take `gear` (all but Latest; Trending has no paging and was 1 IR in 10, 10 in 10 with `gear=cab`)
  - downloads come from www.tone3000.com with the Bearer token (`.nam` as JSON, IRs as WAV); private or removed tones answer 403/404
  - the Select flow works with a loopback redirect (`http://127.0.0.1:<port>/callback`); if redirect URIs are ever registered, the loopback one must be among them
  - TONE3000 gave official permission for this integration (November 2026)

---

## Repo layout

```
roughstomp/
├── CMakeLists.txt, CMakePresets.json   # presets: debug, release, dist (universal, Developer ID)
├── cmake/NAM.cmake                     # NeuralAmpModelerCore, hidden symbols
├── external/JUCE/                      # submodule, 9.0.4 (RoughTrack's pin)
├── fx-core/                            # JUCE-free DSP: Effect, Pedal, Chain, filters, oversampler,
│   ├── include/fx/, src/pedals/        #   RateBridge, PitchDetector, every pedal
│   └── tests/                          # Catch2, plus regression references in tests/regression
├── ui/include/rs_ui/                   # Family.h (shared with RoughTrack), Pedal.h (the pedals' look)
├── tone3000/                           # rs::t3k: TONE3000 sign-in, API, downloads (+ t3k-tests)
├── plugins/Rig/                        # RoughStomp: processor, editor, amp strips, slots, presets,
│                                       #   NAM capture pedal, TONE3000 browser, standalone app
├── resources/                          # app icon, TONE3000 logos
├── docs/images/                        # README screenshots (rs-rig-snapshot --scale=2)
└── tools/
    ├── render/        # rs-render: a pedal over a WAV
    ├── host-render/   # rs-host-render: the built plugin over a WAV, through its saved state
    ├── rig-snapshot/  # rs-rig-snapshot: a picture of the editor
    ├── t3k/           # rs-t3k: TONE3000 from the command line
    ├── validate.sh    # pluginval
    ├── make_icon.sh, make_release.sh
```

## Resources

- *Designing Audio Effect Plugins in C++* — Will Pirkle
- *DAFX: Digital Audio Effects* — Udo Zölzer (ed.)
- Julius O. Smith's online books (filters, physical audio signal processing)
- JUCE tutorials: DSP module, `AudioProcessorValueTreeState`
- pluginval: https://github.com/Tracktion/pluginval
