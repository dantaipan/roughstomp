# rs-t3k: TONE3000 from the command line, through the plugin's own library
# (rs::t3k). See Main.cpp.
juce_add_console_app(rs-t3k)

target_sources(rs-t3k PRIVATE Main.cpp)

target_compile_definitions(rs-t3k PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
    JUCE_MODAL_LOOPS_PERMITTED=1)   # runDispatchLoopUntil, to wait for the Service

target_link_libraries(rs-t3k
    PRIVATE
        rs::t3k
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
