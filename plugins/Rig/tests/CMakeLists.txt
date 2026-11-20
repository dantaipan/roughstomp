# rig-tests: the rig's own code (MIDI control, tone parameters, presets),
# built from its sources as rs-rig-snapshot is, with Catch2.
juce_add_console_app(rig-tests)

set(rig_source ${CMAKE_SOURCE_DIR}/plugins/Rig/Source)
target_sources(rig-tests PRIVATE
    RigTests.cpp
    ${rig_source}/AmpModel.cpp
    ${rig_source}/AmpStrip.cpp
    ${rig_source}/CapturePedal.cpp
    ${rig_source}/MidiControl.cpp
    ${rig_source}/MidiMapView.cpp
    ${rig_source}/Presets.cpp
    ${rig_source}/RigEditor.cpp
    ${rig_source}/RigProcessor.cpp
    ${rig_source}/SlotView.cpp
    ${rig_source}/Tone3000Browser.cpp
    ${rig_source}/ViewSettings.cpp)
target_include_directories(rig-tests PRIVATE ${rig_source})

target_compile_definitions(rig-tests PRIVATE
    JucePlugin_Name="RoughStomp"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
    JUCE_MODAL_LOOPS_PERMITTED=1)   # to pump messages (the processor's timer, change messages)

target_link_libraries(rig-tests
    PRIVATE
        fx::core
        rs::ui
        rs::t3k
        RigTone3000Assets
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,roughstomp_nam>"
        juce::juce_audio_utils
        juce::juce_dsp
        Catch2::Catch2
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

include(FetchContent)
FetchContent_GetProperties(Catch2)   # fetched by fx-core
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(rig-tests)
