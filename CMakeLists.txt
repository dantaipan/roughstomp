cmake_minimum_required(VERSION 3.22)

# Must be set before project() to take effect on macOS
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

project(RoughStomp VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clangd

option(ROUGHSTOMP_BUILD_TESTS "Build fx-core unit tests (Catch2)" ON)
option(ROUGHSTOMP_COPY_PLUGINS "Copy built plugins to ~/Library/Audio/Plug-Ins" ON)
option(ROUGHSTOMP_BUILD_HOST  "Build JUCE's AudioPluginHost for testing" ON)
option(ROUGHSTOMP_BUILD_TOOLS "Build the rs-render offline renderer" ON)

if(ROUGHSTOMP_BUILD_TESTS)
    enable_testing()
endif()

# JUCE — git submodule at external/JUCE (pin to the version RoughTrack uses)
add_subdirectory(external/JUCE)

# Shared DSP library (no plugin code)
add_subdirectory(fx-core)

# Neural Amp Modeler, for the rig's amp
include(cmake/NAM.cmake)

# Shared pedal GUI (JUCE), then the plugins
add_subdirectory(ui)
add_subdirectory(tone3000)
add_subdirectory(plugins/Overdrive)
add_subdirectory(plugins/Rig)

# Offline tools
if(ROUGHSTOMP_BUILD_TOOLS)
    add_subdirectory(tools/render)
    add_subdirectory(tools/host-render)
    add_subdirectory(tools/rig-snapshot)
    add_subdirectory(tools/make-icon)
    add_subdirectory(tools/t3k)
endif()

# Test host, built into build/<preset>/host/
if(ROUGHSTOMP_BUILD_HOST)
    add_subdirectory(external/JUCE/extras/AudioPluginHost host)
endif()
