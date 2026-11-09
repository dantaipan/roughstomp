# TONE3000 (tone3000.com): sign-in, tone lists, models, downloads and the
# Select flow, for the rig's Amp, Power amp and Cab slots. JUCE-only (no plugin
# code) so it can be tested on its own and reused by RoughTrack. An INTERFACE
# library like rs::ui: the sources compile into each target alongside its own
# JUCE modules.
add_library(roughstomp_t3k INTERFACE)
add_library(rs::t3k ALIAS roughstomp_t3k)

target_sources(roughstomp_t3k INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Http.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Pkce.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Types.cpp)

target_include_directories(roughstomp_t3k INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The publishable key (client ID). TONE3000 says it's safe to ship in apps, but
# it stays out of this public repo: put it in tone3000/client_id.local (git
# ignores it; every preset, dist included, reads it) or configure with
# -DROUGHSTOMP_T3K_CLIENT_ID=t3k_pub_... Without one, TONE3000 isn't offered.
set(ROUGHSTOMP_T3K_CLIENT_ID "" CACHE STRING "TONE3000 publishable key (client ID); default: tone3000/client_id.local")
set(roughstomp_t3k_key "${ROUGHSTOMP_T3K_CLIENT_ID}")
set(roughstomp_t3k_key_file "${CMAKE_CURRENT_SOURCE_DIR}/client_id.local")
if(roughstomp_t3k_key STREQUAL "" AND EXISTS "${roughstomp_t3k_key_file}")
    file(STRINGS "${roughstomp_t3k_key_file}" roughstomp_t3k_key LIMIT_COUNT 1)
    string(STRIP "${roughstomp_t3k_key}" roughstomp_t3k_key)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${roughstomp_t3k_key_file}")
if(roughstomp_t3k_key STREQUAL "")
    message(STATUS "TONE3000: no client ID (tone3000/client_id.local), so it won't be offered")
endif()
target_compile_definitions(roughstomp_t3k INTERFACE ROUGHSTOMP_T3K_CLIENT_ID="${roughstomp_t3k_key}")

target_link_libraries(roughstomp_t3k INTERFACE
    juce::juce_core
    juce::juce_events
    juce::juce_cryptography
    juce::juce_graphics)

if(ROUGHSTOMP_BUILD_TESTS)
    juce_add_console_app(t3k-tests)
    target_sources(t3k-tests PRIVATE
        tests/ApiTests.cpp
        tests/PkceTests.cpp
        tests/TypesTests.cpp)
    target_compile_definitions(t3k-tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1
        T3K_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(t3k-tests
        PRIVATE
            rs::t3k
            Catch2::Catch2WithMain
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    include(FetchContent)
    FetchContent_GetProperties(Catch2)   # fetched by fx-core
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(t3k-tests)
endif()
