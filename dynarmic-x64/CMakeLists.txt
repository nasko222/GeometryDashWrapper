cmake_minimum_required(VERSION 3.20)
project(GeometryDashV22BetaBringup18 LANGUAGES C CXX)

if(NOT DEFINED DYNARMIC_SOURCE_DIR)
    message(FATAL_ERROR "Pass -DDYNARMIC_SOURCE_DIR=<official Dynarmic source directory>")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_IGNORE_ASSERTS ON CACHE BOOL "" FORCE)
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
set(DYNARMIC_USE_LLVM OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)

add_subdirectory("${DYNARMIC_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/dynarmic" EXCLUDE_FROM_ALL)

set(GD_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

add_executable(GeometryDashDynarmicProbe
    "${GD_ROOT}/src/dynarmic_probe.cpp"
    "${GD_ROOT}/src/storage_win.c"
    "${GD_ROOT}/src/audio_win.c"
    "${GD_ROOT}/src/apk_extract_audio.c"
    "${GD_ROOT}/src/embedded_effects_stub.c"
    "${GD_ROOT}/third_party/stb/stb_vorbis.c"
    "${GD_ROOT}/third_party/zlib/adler32.c"
    "${GD_ROOT}/third_party/zlib/compress.c"
    "${GD_ROOT}/third_party/zlib/crc32.c"
    "${GD_ROOT}/third_party/zlib/deflate.c"
    "${GD_ROOT}/third_party/zlib/infback.c"
    "${GD_ROOT}/third_party/zlib/inffast.c"
    "${GD_ROOT}/third_party/zlib/inflate.c"
    "${GD_ROOT}/third_party/zlib/inftrees.c"
    "${GD_ROOT}/third_party/zlib/trees.c"
    "${GD_ROOT}/third_party/zlib/uncompr.c"
    "${GD_ROOT}/third_party/zlib/zutil.c"
)

target_include_directories(GeometryDashDynarmicProbe PRIVATE
    "${GD_ROOT}/src"
    "${GD_ROOT}/third_party/zlib"
)
target_compile_definitions(GeometryDashDynarmicProbe PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    crc32=gd_z_crc32
)
target_compile_options(GeometryDashDynarmicProbe PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX>:-O3>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
)
target_link_libraries(GeometryDashDynarmicProbe PRIVATE
    dynarmic
    opengl32
    user32
    gdi32
    winmm
    ole32
    ws2_32
    shell32
    psapi
)

set_target_properties(GeometryDashDynarmicProbe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/probe-output"
)
