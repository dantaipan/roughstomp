add_library(fx-core STATIC
    src/Oversampler.cpp
    src/effects/Gain.cpp
)
add_library(fx::core ALIAS fx-core)

target_include_directories(fx-core PUBLIC include)
target_compile_features(fx-core PUBLIC cxx_std_20)

# Linked into plugin shared libraries, so it must be position independent
set_target_properties(fx-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Strict warnings for our own code only (PRIVATE, so consumers like RoughTrack
# are unaffected). Roughly matches juce_recommended_warning_flags, which the
# plugins compile these headers with anyway.
add_library(fx-warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(fx-warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wcast-align -Wnon-virtual-dtor -Woverloaded-virtual -Wunused
        -Wzero-as-null-pointer-constant)
endif()
target_link_libraries(fx-core PRIVATE fx-warnings)

if(ROUGHSTOMP_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)

    add_executable(fx-core-tests
        tests/FilterTests.cpp
        tests/GainTests.cpp
        tests/OversamplerTests.cpp
        tests/SmootherTests.cpp
        tests/WaveshaperTests.cpp
    )
    target_link_libraries(fx-core-tests PRIVATE fx::core fx-warnings Catch2::Catch2WithMain)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(fx-core-tests)
endif()
