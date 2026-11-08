add_library(fx-core STATIC
    src/Chain.cpp
    src/Oversampler.cpp
    src/Pedals.cpp
    src/PitchDetector.cpp
    src/RateBridge.cpp
    src/pedals/Chorus.cpp
    src/pedals/Delay.cpp
    src/pedals/Drives.cpp
    src/pedals/Dynamics.cpp
    src/pedals/Eq.cpp
    src/pedals/Modulation.cpp
    src/pedals/Reverb.cpp
    src/pedals/Spring.cpp
    src/pedals/Wah.cpp
    src/effects/Gain.cpp
    src/effects/Overdrive.cpp
)
add_library(fx::core ALIAS fx-core)

target_include_directories(fx-core PUBLIC include)
target_compile_features(fx-core PUBLIC cxx_std_20)

# Linked into plugin shared libraries, so it must be position independent.
# Hidden symbols keep a plugin's copy of fx-core separate from any other copy
# in the same process (e.g. RoughTrack linking fx-core and loading our plugin):
# macOS otherwise merges their inline statics.
set_target_properties(fx-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

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
        tests/ChainTests.cpp
        tests/FilterTests.cpp
        tests/GainTests.cpp
        tests/LoopPedalTests.cpp
        tests/ModulationTests.cpp
        tests/OverdriveTests.cpp
        tests/OversamplerTests.cpp
        tests/PedalTests.cpp
        tests/Phase6Tests.cpp
        tests/RateBridgeTests.cpp
        tests/RegressionTests.cpp
        tests/SmootherTests.cpp
        tests/WaveshaperTests.cpp
    )
    target_link_libraries(fx-core-tests PRIVATE fx::core fx-warnings Catch2::Catch2WithMain)
    target_include_directories(fx-core-tests PRIVATE src)   # tests may use pedals' own helpers
    target_compile_definitions(fx-core-tests PRIVATE FX_REGRESSION_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/regression")

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(fx-core-tests)
endif()
