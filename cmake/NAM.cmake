# Neural Amp Modeler core (MIT), the same version RoughTrack uses.
# SOURCE_SUBDIR points nowhere so only the sources arrive: the project's own
# CMakeLists builds its tools, and has no library target to link.
include(FetchContent)
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES Dependencies/eigen
    SOURCE_SUBDIR  no-cmake-project)
FetchContent_MakeAvailable(NeuralAmpModelerCore)

file(GLOB roughstomp_nam_sources CONFIGURE_DEPENDS
    ${neuralampmodelercore_SOURCE_DIR}/NAM/*.cpp
    ${neuralampmodelercore_SOURCE_DIR}/NAM/wavenet/*.cpp)

add_library(roughstomp_nam STATIC ${roughstomp_nam_sources})
add_library(rs::nam ALIAS roughstomp_nam)

target_include_directories(roughstomp_nam SYSTEM
    PUBLIC
        ${neuralampmodelercore_SOURCE_DIR}
        ${neuralampmodelercore_SOURCE_DIR}/Dependencies/eigen
        ${neuralampmodelercore_SOURCE_DIR}/Dependencies/nlohmann)

target_compile_definitions(roughstomp_nam PUBLIC NAM_SAMPLE_FLOAT)
target_compile_features(roughstomp_nam PUBLIC cxx_std_20)

# Linked into plugin shared libraries; third-party code, so no warnings.
# Hidden symbols are essential: macOS merges exported "weak" C++ symbols
# (inline statics, template data) across everything in a process. RoughTrack
# has its own NAM, and without this the plugin's NAM would register its model
# types in RoughTrack's registry, which throws and crashes the host.
set_target_properties(roughstomp_nam PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_compile_options(roughstomp_nam PRIVATE -w)
