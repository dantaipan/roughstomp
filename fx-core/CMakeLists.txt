add_library(fx-core STATIC
    src/effects/Gain.cpp
)
add_library(fx::core ALIAS fx-core)

target_include_directories(fx-core PUBLIC include)
target_compile_features(fx-core PUBLIC cxx_std_20)

# Linked into plugin shared libraries, so it must be position independent
set_target_properties(fx-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ROUGHSTOMP_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(fx-core-tests
        tests/GainTests.cpp
    )
    target_link_libraries(fx-core-tests PRIVATE fx::core Catch2::Catch2WithMain)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(fx-core-tests)
endif()
