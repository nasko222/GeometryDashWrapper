set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)
if(NOT DEFINED ENV{ZIG} OR
   NOT DEFINED ENV{GD_ARM_ZIGAR} OR
   NOT DEFINED ENV{GD_ARM_ZIGRANLIB})
    message(FATAL_ERROR "Set ZIG, GD_ARM_ZIGAR, and GD_ARM_ZIGRANLIB")
endif()

# Never place raw Windows backslashes in CMake's generated compiler files.
# CMake parses those files as source code, where paths such as D:\GD... contain
# invalid escape sequences. Forward slashes work with Windows, cmd.exe and Zig.
string(REPLACE "\\" "/" GD_ARM_ZIG_CMAKE "$ENV{ZIG}")
string(REPLACE "\\" "/" GD_ARM_ZIGAR_CMAKE "$ENV{GD_ARM_ZIGAR}")
string(REPLACE "\\" "/" GD_ARM_ZIGRANLIB_CMAKE "$ENV{GD_ARM_ZIGRANLIB}")

# CMake supports required compiler arguments as list items. Invoke Zig directly
# instead of forwarding through a .cmd script: reconstructing arguments in a
# batch file can turn -std=gnu11 into the invalid bare option -std.
set(CMAKE_C_COMPILER
    "${GD_ARM_ZIG_CMAKE}" cc -target x86-windows-gnu
    CACHE STRING "Zig C compiler command" FORCE)
set(CMAKE_AR "${GD_ARM_ZIGAR_CMAKE}" CACHE FILEPATH "Zig archive wrapper" FORCE)
set(CMAKE_RANLIB "${GD_ARM_ZIGRANLIB_CMAKE}" CACHE FILEPATH "Zig ranlib wrapper" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
