# RoughStomp — Development Plan

A complete guitar rig built with JUCE, developed in VS Code: pedals before the amps, one or two NAM amp models each with its cabinet IR, and an effects loop, in one plugin (VST3/AU) and a standalone app. Load it in RoughTrack (or any DAW) the way you would AmpliTube. Single pedals, starting with the Overdrive, also ship as their own plugins. The DSP lives in a shared library, `fx-core`.

```
guitar -> [ pre-amp pedals ] -┬-> AMP A (NAM) -> CAB A (IR) -> pan -┬-> [ FX loop pedals ] -> out
           tuner, wah, drive  └-> AMP B (NAM) -> CAB B (IR) -> pan -┘    EQ, chorus, delay, reverb
                                  empty by default  "same as A" when empty
```

**About the loop position:** a real loop sits between preamp and power amp. Most NAM captures are a whole amp, so the loop runs after the amps and their cabs, as studio effects do (modulation and ambience sound right there, and in stereo). Until 0.6 there was a power-amp slot after the loop; sessions that used it get its capture in an FX loop slot instead.

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
- [x] Power amp slot: optional second NAM capture after the loop (one instance per stereo side). Replaced by Amp B in the dual-amp rig
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
- [x] **EQ** — 7-band graphic (octaves from 100 Hz, ±15 dB) plus level, on sliders; the caps show the curve
- [x] **Chorus** — modulated 8–16 ms delay mixed with dry; 1–3 voices, stereo width
- [x] **Delay** — digital, tape and ping-pong; free or synced to the host tempo; tails ring out when bypassed. Tape reworked 2026-11-08 as a speed-set tape echo: head bandwidth and wow/flutter/drift scale with the delay time (bright, steady slap-back; dark, wobbly long echoes), saturation on record, thinner repeats; Slapback factory preset
- [x] **Reverb** — 8-line FDN with room, hall and plate voicings; pre-delay, tone; tails ring out when bypassed; every mode and size at the same loudness
- [x] **Spring reverb** — rebuilt after a blind A/B on 2026-11-08: three springs summed mostly to mono, each a modulated delay loop through 100 stretched allpasses (the Välimäki–Parker–Abel model) with a 4.4 kHz cut-off, so echoes chirp and stay dark; dwell (drive), tone (output brightness) and decay
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
- [x] Family look with RoughTrack: `ui/include/rs_ui/Family.h` mirrors RoughTrack's console look (palette, regular-weight type, buttons, menus, console knobs, fader caps); the rig's chrome (header, the Input / Amp / Power amp / Cab / Output row and the Loop box, all one box style) and tuner (now RoughTrack's strobe) use it; only the pedals keep a hardware look
- [x] RoughTrack uses `rs_ui/Family.h` directly (RoughStomp is public at github.com/dantaipan/roughstomp; RoughTrack fetches it at a pinned commit) — on RoughTrack's `family-look` branch, to merge

- [x] More drives: Fuzz (vintage two-transistor style, with Bias for the gated sound), Big Fuzz (cascaded, sustaining, scooped tone), Clean Drive (transparent: clean side kept, treble) — shared 8x-oversampled `DrivePedal` base; levels fitted to sit with the Overdrive. Blues Drive added 2026-11-08: two stages with diodes in the feedback (clean signal kept under the clipping, so it's touch-sensitive), full lows, active treble Tone; Blues factory preset
- [x] Notarized download: `tools/make_release.sh` makes `dist/RoughStomp-<version>.dmg` with the standalone app and the VST3/AU (universal, Developer ID, hardened runtime, notarized and stapled); the app has its own icon (since 2026-11-10 from `resources/icon/roughstomp-icon.svg`, made with Claude Design, rendered by `tools/make_icon.sh`) and starts with the input on
- [x] TONE3000: browse and load captures (Amp, Power amp) and IRs (Cab) from tone3000.com, 2026-11-09. `tone3000/` (`rs::t3k`, JUCE-only, `t3k-tests` against a fake server): device-code sign-in shared by every process, lists, models (A2 first), downloads kept for offline recall, the Select flow through a loopback redirect, 429 back-off. The rig: a TONE3000 button per file box, `Tone3000Browser` over the board (splash, sign-in, Favorites/Created/Downloaded/Trending/Latest, Browse TONE3000, a tone's models and Load), the T3K mark and tone name on a loaded slot, the source saved with sessions and presets (and downloaded again if the file has gone). TONE3000 gave official permission for this integration (November 2026). Phase 0 spike (`tools/t3k`, `rs-t3k`) findings:
  - Device-code sign-in works; access tokens last 3600 s and the refresh token rotates on every refresh (so processes sharing a session must re-read it before refreshing)
  - Without `architecture`, A2 models are hidden, and nearly every current capture is A2-only; `architecture=2` returns only A2 (a tone with both lists the capture once per format). So: ask for A2, fall back to unset (A1 + custom) when there are none. IRs ignore `architecture`
  - Downloads (`model_url` with the Bearer token) come from www.tone3000.com: `.nam` as JSON, IRs as WAV (one was 44 s of 24-bit stereo); A2 and A1 captures and the IR all load in the plugin
  - The Select flow works with a loopback redirect (`http://127.0.0.1:<any port>/callback`, no redirect URIs registered); the callback brings `code`, `state`, `tone_id`. If redirect URIs are ever registered in TONE3000's settings, the loopback one must be among them
  - Tone images are JPEG; private or removed tones answer 403 or 404
- [x] Octave pedal, 2026-11-10: Direct, Oct -1, Oct -2, Oct Up, Range, Vintage/Poly, Tone. Vintage is the analog divider: a 4-pole tracking filter at 160 Hz (so a low string's weak fundamental, not its louder harmonics, makes the zero crossings), a Schmitt trigger relative to a fast envelope, two flip-flops, their squares shaped by the whole guitar's envelope (from the analytic signal, a 90-degree all-pass splitter, so it can fall within 2 ms between notes without rippling; a slow low-end envelope smeared fast passages into a drone); a full-wave rectifier for the octave up. Poly is a filter bank: 22 quarter-octave bands of two complex one-poles (at least 30 Hz wide, so the low ones respond quickly), phase-corrected so bands carrying a note agree, then the square root (kept continuous; a band that drops out restarts from a common point) for the octaves down and the square for the octave up, with smooth gates (absolute and relative) and no per-band decisions. Tone is a low-pass on the octaves down; a +6 dB shelf below 100 Hz and 100% set 4 dB over the dry signal, because the ear needs it down there. Lessons: test on fast passages, not only sustained notes (per-band "is this a note" decisions flickered and dropped notes); measured on a guitar take by tracking its pitch and checking each 10 ms frame for the octave, in tune and present (both modes: no dropouts, Poly 0% and Vintage under 1% of the fast passage out of tune, 4-10 ms to respond)
- [x] Slider view, 2026-11-12: a Pedals / Sliders switch in the header (a global preference, `ViewSettings`, in ~/Library/Application Support/RoughStomp); in the slider view each slot is a plain family-coloured box with the pedal's colour as a strip, a row per control (name, value, slider), menus, and an On button; the EQ keeps its faders
- [x] NAM captures in pedal slots, 2026-11-12: "NAM Capture" (`CapturePedal`, in the plugin since it needs NAM): Input, Level, Mix, one model per channel (twice in the loop), its dry path delayed to match its resampling latency so Mix and bypass never comb-filter; loaded in the background (at once when restoring for an offline bounce). `rig::FileSlot` now covers pedal slots too, so files and TONE3000 sources (with re-download) work there as for the amp and cab; the browser offers pedal captures first for a slot. Captures travel with swaps (reloaded across rows)
- [x] Dual-amp rig, 2026-11-13: Amp A and Amp B side by side (the pre-amp pedals feed both), each into its own cab, panned (balance law: centred is unity on both sides, so a one-amp session sounds exactly as before), then the FX loop, now after the cabs. Amp B replaces the power amp (`powerOn` is now Amp B's On; an old session's power-amp capture moves into the first free loop slot as a NAM Capture, on or off as it was). Cab B empty uses Cab A's IR ("Same as A"). The shorter amp path is delayed to match the longer one's resampling latency; Amp A off passes the guitar dry, unless Amp B is playing. The top row is one box with a strip per amp (`AmpStrip`): an "Amp A" / "Amp B" checkbox (its On), capture, Gain / Level / Pan, then a "Cab" checkbox (`cabOn` is now Cab A's, `cabBOn` Cab B's) and the cab
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
