# rs-render: run an fx-core effect over a WAV file, offline.
juce_add_console_app(rs-render)

target_sources(rs-render PRIVATE Main.cpp)

target_compile_definitions(rs-render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(rs-render
    PRIVATE
        fx::core
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
