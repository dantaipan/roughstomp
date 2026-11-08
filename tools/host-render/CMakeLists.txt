# rs-host-render: load a built plugin like a DAW would, set it up through its
# saved state, and render a WAV file through it. Tests the whole plugin,
# including session restore.
juce_add_console_app(rs-host-render)

target_sources(rs-host-render PRIVATE Main.cpp)

target_compile_definitions(rs-host-render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(rs-host-render
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_processors
        # AU hosting: JUCE's generic AU editor needs these
        "-framework AudioUnit"
        "-framework CoreAudioKit"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
