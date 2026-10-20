cmake_minimum_required(VERSION 3.15)
project(DynamicHitFeedback VERSION 1.0.0 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Use the MSVC toolchain (Visual Studio 2019/2022). GTA San Andreas plugins must be 32-bit MSVC builds.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "GTA San Andreas is a 32-bit game. Configure with:  cmake -S . -B build -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: no Visual C++ redistributable needed on the player's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(DynamicHitFeedback SHARED
  src/main.cpp
  src/Config.cpp
  src/GameSA.cpp
  src/Log.cpp
  src/Reactions.cpp
  src/Skeleton.cpp)

target_compile_definitions(DynamicHitFeedback PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(DynamicHitFeedback PRIVATE /W3 /MP)
set_target_properties(DynamicHitFeedback PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "DynamicHitFeedback")

# Drop the default ini next to the .asi so the build output is ready to copy into the game folder.
add_custom_command(TARGET DynamicHitFeedback POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/DynamicHitFeedback.ini" "$<TARGET_FILE_DIR:DynamicHitFeedback>")
