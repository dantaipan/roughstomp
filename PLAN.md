# RoughStomp — Development Plan

A complete guitar rig built with JUCE, developed in VS Code: pedals before the amp, a NAM amp model, an effects loop before the power amp, and a cabinet IR, in one plugin (VST3/AU) and a standalone app. Load it in RoughTrack (or any DAW) the way you would AmpliTube. Single pedals, starting with the Overdrive, also ship as their own plugins. The DSP lives in a shared library, `fx-core`.

```
guitar -> [ pre-amp pedals ] -> AMP (NAM) -> [ FX loop pedals ] -> POWER AMP (optional NAM) -> CAB (IR) -> out
           tuner, wah, drive                  EQ, chorus, delay,    empty by default: most
                                              reverb                captures include the power amp
```

**About the loop position:** a real loop sits between preamp and power amp. Most NAM captures are a whole amp, so by default the loop runs after the amp model, before the cab (what modelling software usually does; modulation and ambience sound right there). For a true split, load a preamp-only capture as the amp and a power-amp capture in the power amp slot.

---

## Guiding principles

- **One DSP core, many front-ends.** Every effect is a plain C++ class with no JUCE plugin dependencies. Plugin wrappers and RoughTrack both use it.
- **Real-time safe audio thread.** No allocation, locks, file I/O or logging in `process()`. Allocate in `prepare()`.
- **Smooth everything.** Every user parameter goes through a smoother to avoid zipper noise.
- **Test by ear and by numbers.** Unit tests for DSP math, pluginval for hosts, listening tests for tone.
- **Ship small.** Each phase ends with something that runs in a DAW.

---

## Repo layout

```
roughstomp/
├── CMakeLists.txt
├── CMakePresets.json
├── PLAN.md
├── external/
│   └── JUCE/                  # git submodule (or fetched with CPM)
├── fx-core/                   # shared DSP library — no plugin code
│   ├── include/fx/
│   │   ├── Effect.h           # common interface
│   │   ├── Smoother.h
│   │   ├── Oversampler.h
│   │   ├── Waveshapers.h
│   │   ├── Filters.h
│   │   ├── Lfo.h              # phase 5
│   │   ├── DelayLine.h        # phase 5
│   │   └── effects/
│   │       ├── Overdrive.h/.cpp
│   │       └── ...
│   └── tests/                 # Catch2 unit tests
├── plugins/
│   ├── Overdrive/             # JUCE plugin wrapper + editor
│   ├── Pedalboard/            # phase 4
│   └── ...
├── ui/                        # shared pedal look-and-feel, knobs, footswitch
├── presets/
└── tools/
    └── render/                # offline WAV renderer for A/B and null tests
```

---

## Phase 0 — Project setup

**Goal:** an empty plugin that builds, loads in a host and in RoughTrack.

- [x] Create repo, add JUCE as a submodule (pin the same JUCE version RoughTrack uses)
- [x] VS Code extensions: CMake Tools, clangd (or C/C++), CodeLLDB
- [x] `CMakePresets.json` with Debug and Release presets (Ninja generator)
- [x] `juce_add_plugin` target with formats `VST3 AU Standalone`
- [x] `fx-core` as a static library target; plugin links to it
- [x] Build JUCE's AudioPluginHost for quick testing
- [x] Install pluginval; add a `tools/validate.sh` script
- [x] VS Code `launch.json`: debug the plugin by launching AudioPluginHost (or RoughTrack) as the host executable
- [x] Pass-through plugin loads, passes pluginval at strictness 5+

**Done when:** pass-through audio works in AudioPluginHost, a commercial DAW and RoughTrack.

---

## Phase 1 — DSP core foundations

**Goal:** the building blocks every pedal will reuse.

- [x] `Effect` interface:
  ```cpp
  struct Effect {
      virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
      virtual void reset() = 0;
      virtual void process(float* const* channels, int numChannels, int numSamples) = 0;
      virtual int  getLatencySamples() const { return 0; }
      virtual ~Effect() = default;
  };
  ```
- [x] Parameter smoother (linear and one-pole)
- [x] Oversampler: own polyphase halfband FIR cascade (keeps fx-core JUCE-free so RoughTrack can link it without a second copy of JUCE), 2x/4x/8x, reports latency
- [x] Filters: one-pole LP/HP, biquad (RBJ cookbook), DC blocker
- [x] Waveshapers: `tanh`, soft clip (cubic), hard clip, asymmetric diode-style
- [x] Catch2 tests: filter frequency response, DC blocker removes offset, smoother converges, oversampler round-trip
- [x] `tools/render`: CLI that runs an effect over a WAV file with given parameters → output WAV (great for A/B comparisons and regression tests)

**Done when:** tests pass and the render tool can process a DI guitar file.

---

## Phase 2 — Overdrive v1

**Goal:** a usable, good-sounding overdrive.

Signal flow:
```
in → [oversample ×8] → drive (boost above ~720 Hz, lows at unity) → pre-clip LPF 7 kHz
   → tanh soft clipper (threshold -12 dBFS) → tone (1-pole LPF, 600 Hz–8 kHz)
   → fizz LPF 10 kHz → DC blocker → makeup + level → clean blend
   → bypass crossfade → [downsample] → out
(blend and bypass run oversampled too, so dry/wet stay time-aligned and latency is constant)
```

- [x] Parameters (`AudioProcessorValueTreeState`): Drive, Tone, Level, Bypass
- [x] Drive mapped on a log/audio taper; gain range roughly 0–40 dB
- [x] Clean-blend/Mix parameter (optional, useful for bass)
- [x] Click-free bypass (short crossfade)
- [x] Report oversampler latency to host
- [x] Basic generic editor first, then a simple pedal GUI (3 knobs + footswitch + LED)
- [x] State save/restore; 3–5 factory presets
- [x] Test: aliasing check with a high-frequency sine sweep (spectrum should stay clean), CPU usage per instance, pluginval pass

- [x] Automatic makeup gain (-1 dB per 4 dB of drive) so loudness stays within ~1 dB of bypass

**Done when:** it sounds musical on DI guitar into an amp sim, has no clicks or aliasing hash, and passes pluginval.

---

## Phase 3 — Overdrive v2 (character and realism)

- [x] Asymmetric clipping option (even harmonics)
- [x] Clipping "Mode" switch: soft (TS-style), hard (distortion), asymmetric — each with its own voicing (focus, gain, fizz filter, loudness trim); switching crossfades
- [x] Input-level sensitivity: responds to picking dynamics and guitar volume — envelope-driven clipper bias: dug-in picking blooms with even harmonics, rolled-back volume cleans up
- [x] Better tone stack (e.g. Big Muff-style or Tube Screamer tone network approximation) — tried a TS-style 700 Hz shelf; it lost the blind A/B (too much fizz above 3 kHz), so the tone stays a 600 Hz–8 kHz swept lowpass
- [ ] Optional (deferred): circuit-modelled stage (wave digital filters or nodal analysis) as a learning exercise
- [x] Shared `ui/` pedal look-and-feel reused by every future pedal — `rs::ui`: look-and-feel, knob, footswitch, LED, mini toggle, enclosure

**Done when:** v2 is a clear step up from v1 in blind A/B listening.

*Result:* round 1 (v2 Soft with the 700 Hz shelf) lost 0–3 to v1: too much fizz above 3 kHz.
After restoring v1's swept-lowpass tone, round 2 went 3–1 to v2: Hard beat v1 at medium
and high drive (fuller, warmer), Asym split 1–1 (very close). Soft now matches v1's tone.

---

## Phase 4 — The rig: pedals, amp, loop, cab

**Goal:** play guitar through RoughStomp as a full rig, standalone and in a DAW.

- [x] Pedal registry/factory in `fx-core` (create effects by ID) — `fx::Pedal` describes its own controls (`PedalInfo`); `fx::registry` has Overdrive and Boost
- [x] Chain processing without allocation on the audio thread (build new chain on message thread, swap atomically) — `fx::Chain`; pedals are shared between layouts so reordering never resets them; stress-tested under ThreadSanitizer
- [x] RoughStomp rig plugin (VST3, AU, Standalone): mono guitar in, stereo out — pluginval strictness 10 passes
- [x] Pre-amp section: 6 pedal slots, each holding any pedal, with per-slot bypass — slot knobs are fixed host parameters mapped onto whichever pedal is in the slot
- [x] Amp: load a NAM capture (ported from RoughTrack, loudness-normalised), input gain, level, on/off
- [x] Resample to the capture's rate (most are 48 kHz), so a 44.1 kHz session or device still works — fx::RateBridge, windowed sinc, fixed whole-sample latency (66 samples at 44.1 kHz). Cabinet IRs go through the same resampler: JUCE's own let through highs the cab should cut (up to 60 dB too much above 16 kHz)
- [x] FX loop section: 6 pedal slots; send, return, mix and loop on/off — the dry path is delayed by the loop's latency, so blending never comb-filters
- [x] Power amp slot: optional second NAM capture after the loop (one instance per stereo side)
- [x] Cab: load an IR (WAV/AIFF), on/off; off when the capture already includes a cab
- [x] Input and output level; total latency (pedals, plus amp resampling) reported to the host
- [x] Session state saves pedals, settings and file paths — missing files are reported and their paths kept
- [x] `rs-host-render`: renders a WAV through the built plugin, configured through its saved state

**Done when:** standalone and in RoughTrack you can play Overdrive -> NAM amp -> loop -> cab, and it comes back the same after reopening.

---

## Phase 5 — Loop effects

Shared building blocks first:
- [x] `Lfo`: sine, triangle, square, random/S&H; rate in Hz; stereo phase offset — square has short slopes and random glides, so neither clicks
- [x] Tempo-synced LFO rates — a Sync menu (1 bar to 1/16, triplets) on every modulation pedal; the host tempo reaches pedals through `Pedal::setTempo`
- [x] Lock synced LFOs' phase to the host's bar position — while the host plays, cycle n starts on beat n x the division (`Pedal::setPlayPosition`)
- [x] `DelayLine`: circular buffer with fractional delay — linear and 4-point Lagrange (cubic); allpass not needed so far

Pedals (all stereo):
- [x] **EQ** — 7-band graphic (octaves from 100 Hz, ±15 dB) plus level, on sliders with the real response curve drawn behind them (`fx/GraphicEq.h` is shared by the pedal and the GUI)
- [x] **Chorus** — modulated 8–16 ms delay mixed with dry; 1–3 voices, stereo width
- [x] **Delay** — digital, tape (darker, saturating, wow and flutter) and ping-pong; free or synced to the host tempo; tails ring out when bypassed
- [x] **Reverb** — 8-line FDN with room, hall and plate voicings; pre-delay, tone; tails ring out when bypassed; every mode and size at the same loudness
- [x] **Spring reverb** — two springs (one per side): delay loops through 100 dispersive allpasses for the chirp, with dwell (drive), tone and decay
- [x] **Tremolo** (sine/triangle/square, stereo spread up to auto-pan), **Vibrato**, **Flanger** (+-95% feedback), **Phaser** (4/8/12 stages, feedback)
- [x] Tests: no clicks when rate/depth change, tempo sync accuracy, interpolation quality (no zipper or HF loss)

**Done when:** Overdrive -> amp -> [EQ, Chorus, Delay, Reverb] -> cab sounds like a finished rig in a DAW, with tempo sync working.

---

## Phase 6 — Usability

- [x] Rig presets: save, recall and browse whole rigs (pedals, amp, cab, settings) — user presets are `.rsrig` files in `~/Library/Application Support/RoughStomp/Presets`; seven factory presets keep your amp and cab; "*" marks changes since loading
- [x] Drag pedals to reorder, and between the pre-amp and loop sections — drop on a slot to swap (or move into an empty one); settings travel with the pedal
- [x] Tuner (mutes while tuning) — YIN pitch detection within 0.5 cent from low E to C6; header button, display over the amp row
- [x] Noise gate at the input (hysteresis, hold, click-free fades); compressor pedal (sustain, attack, level, blend)
- [x] Wah (with host-automatable position for an expression pedal) — pedal or auto (envelope) mode
- [x] Polished rig GUI: pedalboard rails, amp face (tolex, piping, name plate), cab grille, resizable window (70–150%, remembered)

---

## Phase 7 — Later ideas

Loose ends from earlier phases, done first:
- [x] Regression renders for every pedal (`fx-core-tests "[regression]"`, references in `fx-core/tests/regression`); `rs-render` takes any pedal from the registry
- [x] Amp, power amp and cab on/off crossfade (20 ms) against a latency-aligned dry signal instead of clicking
- [x] Capture loading in the background: choosing an amp or loading a preset doesn't freeze the GUI (session restore stays synchronous, for offline bounces)

- [x] More drives: Fuzz (vintage two-transistor style, with Bias for the gated sound), Big Fuzz (cascaded, sustaining, scooped tone), Clean Drive (transparent: clean side kept, treble) — shared 8x-oversampled `DrivePedal` base; levels fitted to sit with the Overdrive
- [ ] Stereo or dual-amp rigs
- [ ] Optional (deferred): circuit-modelled stage (wave digital filters or nodal analysis) as a learning exercise
- [ ] CLAP format

---

## Conventions

- C++20, `float` audio path, `double` for LFO/phase accumulators
- Effects never touch JUCE GUI or `AudioProcessor`; wrappers own parameters and push values to the effect each block
- Parameter IDs are stable once released (presets depend on them)
- Every effect gets: unit tests, a render-tool regression file, a pluginval pass

## Resources

- *Designing Audio Effect Plugins in C++* — Will Pirkle
- *DAFX: Digital Audio Effects* — Udo Zölzer (ed.)
- Julius O. Smith's online books (filters, physical audio signal processing)
- JUCE tutorials: DSP module, `AudioProcessorValueTreeState`, `AudioProcessorGraph`
- pluginval: https://github.com/Tracktion/pluginval
