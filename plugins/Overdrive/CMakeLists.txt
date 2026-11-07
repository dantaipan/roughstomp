juce_add_plugin(Overdrive
    COMPANY_NAME             "RoughStomp"
    BUNDLE_ID                "com.roughstomp.overdrive"
    PLUGIN_MANUFACTURER_CODE Rstm          # 4 chars, at least one uppercase
    PLUGIN_CODE              Odrv          # 4 chars, unique per plugin
    FORMATS                  VST3 AU Standalone
    PRODUCT_NAME             "RoughStomp Overdrive"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE          # installs to ~/Library/Audio/Plug-Ins
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Distortion)

target_sources(Overdrive PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h)

target_compile_definitions(Overdrive PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Overdrive
    PRIVATE
        fx::core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
