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

The build copies the plugin to `~/Library/Audio/Plug-Ins/VST3` and `.../Components` automatically, so DAWs see it after a rescan.

## First run in AudioPluginHost

1. F5 with *Debug in AudioPluginHost*
2. Options → Edit the list of available plug-ins → scan VST3 / AU
3. Right-click the graph → add *RoughStomp Overdrive*; wire Audio Input → plugin → Audio Output
4. File → Save the graph as `tools/test.filtergraph` so it reopens next time

Breakpoints in `plugins/` and `fx-core/` hit inside the host.

## Layout

- `fx-core/` — DSP only, no JUCE plugin code; linked by plugins and RoughTrack
- `plugins/Overdrive/` — JUCE wrapper (parameters, state, editor)
- `tools/validate.sh` — pluginval runner

## Using fx-core in RoughTrack

Add this repo as a submodule of RoughTrack (or point at it), then:

```cmake
add_subdirectory(path/to/roughstomp/fx-core fx-core)
target_link_libraries(RoughTrack PRIVATE fx::core)
```

Set `ROUGHSTOMP_BUILD_TESTS=OFF` in RoughTrack's build to skip the tests.
