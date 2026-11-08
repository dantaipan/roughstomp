# rs-make-icon: draws RoughStomp's app icon in code and writes the macOS icon
# set (resources/icon/AppIcon.iconset). The PNGs are committed; run this only
# to change the icon:  rs-make-icon resources/icon/AppIcon.iconset
juce_add_console_app(rs-make-icon)

target_sources(rs-make-icon PRIVATE Main.cpp)

target_compile_definitions(rs-make-icon PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

target_link_libraries(rs-make-icon
    PRIVATE
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
