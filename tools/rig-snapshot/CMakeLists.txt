# rs-rig-snapshot: build the rig's editor from its sources, set it up like
# rs-host-render does, and save a picture of it. For checking the GUI without
# a DAW (a hosted plugin's editor is a native view that can't be captured).
juce_add_console_app(rs-rig-snapshot)

set(rig_source ${CMAKE_SOURCE_DIR}/plugins/Rig/Source)
target_sources(rs-rig-snapshot PRIVATE
    Main.cpp
    ${rig_source}/AmpModel.cpp
    ${rig_source}/RigEditor.cpp
    ${rig_source}/RigProcessor.cpp
    ${rig_source}/SlotView.cpp)
target_include_directories(rs-rig-snapshot PRIVATE ${rig_source})

target_compile_definitions(rs-rig-snapshot PRIVATE
    JucePlugin_Name="RoughStomp"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(rs-rig-snapshot
    PRIVATE
        fx::core
        rs::ui
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,roughstomp_nam>"
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
