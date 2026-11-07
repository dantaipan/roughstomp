# RoughStomp

Guitar effects in JUCE, a companion to RoughTrack. See [PLAN.md](PLAN.md) for the roadmap.

## First-time setup (macOS)

```bash
# 1. Tools
brew install cmake ninja
# Install pluginval from https://github.com/Tracktion/pluginval/releases
#   (copy pluginval.app to /Applications)

# 2. Repo and JUCE (use the same JUCE tag as RoughTrack)
cd roughstomp
git init
git submodule add https://github.com/juce-framework/JUCE.git external/JUCE
cd external/JUCE && git checkout 9.0.4 && cd ../..
chmod +x tools/validate.sh

# 3. Open in VS Code and accept the recommended extensions
code .
```

VS Code (CMake Tools) configures automatically using the **Debug** preset. Catch2 is downloaded on the first configure.

## Everyday use

| What | How |
|---|---|
| Build | ⇧⌘B (Build (Debug) task) |
| Run unit tests | Command Palette → *Tasks: Run Test Task* |
| Validate plugin | *Tasks: Run Task* → *Validate plugin (pluginval)* |
| Debug in AudioPluginHost | Run and Debug → *Debug in AudioPluginHost* (F5) |
| Debug inside RoughTrack | Run and Debug → *Debug in RoughTrack* (prompts for the path) |

Command line equivalent:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
tools/validate.sh debug
```

## Offline rendering (rs-render)

Runs an fx-core effect over a WAV file, the same way a DAW would (block by
block), and writes a latency-compensated 32-bit float WAV that lines up
sample-for-sample with the input. Use it for A/B listening and null tests.

```bash
R=build/debug/tools/render/rs-render_artefacts/Debug/rs-render
$R --list                                   # effects and their parameters
$R gain di.wav out.wav db=-6                # name=value parameters
$R gain di.wav out.wav --block=64 --24bit   # host block size, 24-bit output
```

New effects are added to the registry at the top of `tools/render/Main.cpp`.

The build copies the plugin to `~/Library/Audio/Plug-Ins/VST3` and `.../Components` automatically, so DAWs see it after a rescan.

## First run in AudioPluginHost

1. F5 with *Debug in AudioPluginHost*
2. Options → Edit the list of available plug-ins → scan VST3 / AU
3. Right-click the graph → add *RoughStomp Overdrive*; wire Audio Input → plugin → Audio Output
4. File → Save the graph as `tools/test.filtergraph` so it reopens next time

Breakpoints in `plugins/` and `fx-core/` hit inside the host.

## Layout

- `fx-core/` — DSP only, plain C++20 with no JUCE dependency; linked by plugins and RoughTrack
  - `Smoother.h`, `Filters.h`, `Waveshapers.h`, `Oversampler.h` — shared building blocks
  - `effects/` — complete effects implementing `fx::Effect`
- `plugins/Overdrive/` — JUCE wrapper (parameters, state, editor)
- `tools/validate.sh` — pluginval runner
- `tools/render/` — rs-render offline renderer

## Using fx-core in RoughTrack

Add this repo as a submodule of RoughTrack (or point at it), then:

```cmake
add_subdirectory(path/to/roughstomp/fx-core fx-core)
target_link_libraries(RoughTrack PRIVATE fx::core)
```

fx-core's tests are only built when `ROUGHSTOMP_BUILD_TESTS` is ON, which it isn't inside RoughTrack, so nothing else is needed.
