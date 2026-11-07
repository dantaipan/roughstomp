# RoughStomp — Development Plan

Guitar effects built with JUCE, developed in VS Code. Start with an overdrive, then grow into a pedalboard with an effects loop and modulation effects. The DSP lives in a shared library so the same code can ship as VST3/AU plugins **and** be linked directly into RoughTrack.

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

- [ ] Asymmetric clipping option (even harmonics)
- [ ] Clipping "Mode" switch: soft (TS-style), hard (distortion), asymmetric
- [ ] Input-level sensitivity: responds to picking dynamics and guitar volume
- [ ] Better tone stack (e.g. Big Muff-style or Tube Screamer tone network approximation)
- [ ] Optional: circuit-modelled stage (wave digital filters or nodal analysis) as a learning exercise
- [ ] Shared `ui/` pedal look-and-feel reused by every future pedal

**Done when:** v2 is a clear step up from v1 in blind A/B listening.

---

## Phase 4 — Pedalboard and effects loop

**Goal:** chain multiple pedals and insert outside effects.

- [ ] Pedal registry/factory in `fx-core` (create effects by ID)
- [ ] Pedalboard plugin: fixed number of slots (e.g. 6), each holding any pedal; reorderable by drag
- [ ] Chain processing without allocation on the audio thread (build new chain on message thread, swap atomically)
- [ ] Per-slot bypass, total latency = sum of slot latencies, reported to host
- [ ] **Effects loop slot:** a send/return point in the chain
  - Inside RoughTrack: route to a track/bus or host an external VST3/AU in the loop (`AudioPluginFormatManager`)
  - As a plugin: host an external plugin inside the slot (optional — adds complexity)
- [ ] Loop controls: send level, return level, dry/wet mix, loop on/off
- [ ] Chain state and presets save/restore
- [ ] RoughTrack integration: per-track insert chain using the same pedal registry

**Done when:** you can build Overdrive → [loop: external reverb] → Chorus and save/recall it.

---

## Phase 5 — Modulation effects

Shared building blocks first:
- [ ] `Lfo`: sine, triangle, square, random/S&H; rate in Hz or tempo-synced from host play head; stereo phase offset
- [ ] `DelayLine`: circular buffer with fractional delay (linear, then cubic/Lagrange or allpass interpolation)

Effects, in rough order of difficulty:
- [ ] **Tremolo** — LFO on amplitude (warm-up, very quick)
- [ ] **Vibrato** — modulated short delay, 100% wet
- [ ] **Chorus** — modulated 10–30 ms delay mixed with dry; optional multi-voice
- [ ] **Flanger** — modulated 1–10 ms delay with feedback
- [ ] **Phaser** — 4–12 first-order allpass stages with modulated cutoff, plus feedback
- [ ] Tests: no clicks when rate/depth change, tempo sync accuracy, interpolation quality (no zipper or HF loss)

**Done when:** all five run in the Pedalboard with tempo sync working in a DAW.

---

## Phase 6 — Later ideas

- [ ] Reverb (Freeverb or FDN first, then plate/spring)
- [ ] Delay (tape/analog flavours, ping-pong)
- [ ] Amp + cab sim, with the effects loop placed between preamp and power amp like a real amp
- [ ] Noise gate and compressor (both pair well with drive)
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
