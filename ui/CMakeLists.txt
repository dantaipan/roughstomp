# Shared pedal GUI pieces: look-and-feel, knobs, footswitch, LED, mode toggle
# and enclosure. An INTERFACE library, so the sources compile into each plugin
# alongside its own copy of the JUCE modules (a static library would duplicate
# JUCE's symbols).
add_library(roughstomp_ui INTERFACE)
add_library(rs::ui ALIAS roughstomp_ui)

target_sources(roughstomp_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Pedal.cpp)

target_include_directories(roughstomp_ui INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
