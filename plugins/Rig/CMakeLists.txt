# RoughStomp: the full rig (pre-amp pedals -> NAM amp -> FX loop -> power amp -> cab)
juce_add_plugin(Rig
    COMPANY_NAME             "RoughStomp"
    BUNDLE_ID                "com.roughstomp.rig"
    PLUGIN_MANUFACTURER_CODE Rstm
    PLUGIN_CODE              Rrig
    FORMATS                  VST3 AU Standalone
    PRODUCT_NAME             "RoughStomp"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Distortion)

target_sources(Rig PRIVATE
    Source/AmpModel.cpp
    Source/AmpModel.h
    Source/RigEditor.cpp
    Source/RigEditor.h
    Source/RigProcessor.cpp
    Source/RigProcessor.h
    Source/SlotView.cpp
    Source/SlotView.h)

target_compile_definitions(Rig PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Rig
    PRIVATE
        fx::core
        rs::ui
        # NAM's model types register themselves from static objects that
        # nothing refers to, so link the whole archive or they'd be dropped
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,roughstomp_nam>"
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
