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
    COPY_PLUGIN_AFTER_BUILD  FALSE   # copied below, after signing
    # The app's icon is copied in (below) before signing; JUCE's ICON_BIG would
    # also copy one into the plugins, after they're signed, breaking them
    PLIST_TO_MERGE           "<plist><dict><key>CFBundleIconFile</key><string>AppIcon</string></dict></plist>"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RoughStomp plays your guitar through its rig from your audio interface's input."
    AU_MAIN_TYPE             kAudioUnitType_Effect
    VST3_CATEGORIES          Fx Distortion)

# TONE3000's logos, for its browser (resources/tone3000)
juce_add_binary_data(RigTone3000Assets
    HEADER_NAME Tone3000Assets.h
    NAMESPACE Tone3000Assets
    SOURCES
        ${CMAKE_SOURCE_DIR}/resources/tone3000/tone3000-logo.png
        ${CMAKE_SOURCE_DIR}/resources/tone3000/t3k-logo.png)

target_sources(Rig PRIVATE
    Source/AmpModel.cpp
    Source/AmpModel.h
    Source/CapturePedal.cpp
    Source/CapturePedal.h
    Source/Presets.cpp
    Source/Presets.h
    Source/RigEditor.cpp
    Source/RigEditor.h
    Source/RigProcessor.cpp
    Source/RigProcessor.h
    Source/SlotView.cpp
    Source/SlotView.h
    Source/Tone3000Browser.cpp
    Source/Tone3000Browser.h
    Source/ViewSettings.cpp
    Source/ViewSettings.h)

target_compile_definitions(Rig PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Rig
    PRIVATE
        fx::core
        rs::ui
        rs::t3k
        RigTone3000Assets
        # NAM's model types register themselves from static objects that
        # nothing refers to, so link the whole archive or they'd be dropped
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,roughstomp_nam>"
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# The standalone app: JUCE's, starting with the input ready to play
# (Source/StandaloneApp.cpp)
target_sources(Rig_Standalone PRIVATE Source/StandaloneApp.cpp)
target_compile_definitions(Rig_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_include_directories(Rig_Standalone PRIVATE "${CMAKE_SOURCE_DIR}/ui/include")   # the family colours

#==============================================================================
# Signing, as RoughTrack does: every build is signed with the hardened runtime,
# so a release finds no surprises. Local builds use an Apple Development
# certificate if there is one (otherwise ad hoc); the dist preset uses
# Developer ID, with Apple's secure timestamp for notarization.
execute_process(COMMAND security find-identity -v -p codesigning
    OUTPUT_VARIABLE roughstomp_identities
    ERROR_QUIET)

if(roughstomp_identities MATCHES "Apple Development")
    set(roughstomp_default_identity "Apple Development")
else()
    set(roughstomp_default_identity "-")
endif()

set(ROUGHSTOMP_CODESIGN_IDENTITY "${roughstomp_default_identity}" CACHE STRING "codesign identity")

if(ROUGHSTOMP_CODESIGN_IDENTITY MATCHES "Developer ID")
    set(roughstomp_timestamp "--timestamp")
else()
    set(roughstomp_timestamp "--timestamp=none")
endif()

# JUCE's icon holds only two sizes, so the full set replaces it in the app
file(GLOB roughstomp_icon_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/icon/AppIcon.iconset/*.png")
set(roughstomp_icon "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns")
add_custom_command(OUTPUT "${roughstomp_icon}"
    COMMAND iconutil -c icns -o "${roughstomp_icon}" "${CMAKE_SOURCE_DIR}/resources/icon/AppIcon.iconset"
    DEPENDS ${roughstomp_icon_files}
    VERBATIM)
add_custom_target(RoughStompIcon DEPENDS "${roughstomp_icon}")
add_dependencies(Rig_Standalone RoughStompIcon)
set_property(TARGET Rig_Standalone APPEND PROPERTY LINK_DEPENDS "${roughstomp_icon}")

add_custom_command(TARGET Rig_Standalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "${roughstomp_icon}" "$<TARGET_BUNDLE_CONTENT_DIR:Rig_Standalone>/Resources/AppIcon.icns"
    COMMAND codesign --force --options runtime ${roughstomp_timestamp}
        --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/Entitlements.plist"
        --sign "${ROUGHSTOMP_CODESIGN_IDENTITY}"
        "$<TARGET_BUNDLE_DIR:Rig_Standalone>"
    VERBATIM)

# The plugins: signed, then (for local builds) copied where hosts look
foreach(format VST3 AU)
    set(bundle "$<TARGET_BUNDLE_DIR:Rig_${format}>")
    add_custom_command(TARGET Rig_${format} POST_BUILD
        COMMAND codesign --force --options runtime ${roughstomp_timestamp}
            --sign "${ROUGHSTOMP_CODESIGN_IDENTITY}" "${bundle}"
        VERBATIM)

    if(ROUGHSTOMP_COPY_PLUGINS)
        if(format STREQUAL "VST3")
            set(destination "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
        else()
            set(destination "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
        endif()
        add_custom_command(TARGET Rig_${format} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${destination}/$<TARGET_BUNDLE_DIR_NAME:Rig_${format}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${bundle}" "${destination}/$<TARGET_BUNDLE_DIR_NAME:Rig_${format}>"
            VERBATIM)
    endif()
endforeach()
