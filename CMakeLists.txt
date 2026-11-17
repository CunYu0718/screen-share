cmake_minimum_required(VERSION 3.25)

project(
    ScreenShare
    VERSION 0.1.0
    DESCRIPTION "High-performance Windows screen sharing"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ScreenShareWarnings)

option(SCREEN_SHARE_BUILD_DESKTOP "Build the Qt desktop application" OFF)
option(SCREEN_SHARE_ENABLE_LIVEKIT "Build the LiveKit media adapter" OFF)

add_library(screen_share_project_options INTERFACE)
target_compile_features(screen_share_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(screen_share_project_options INTERFACE /utf-8 /Zc:__cplusplus)
endif()

add_library(screen_share_project_warnings INTERFACE)
screen_share_enable_warnings(screen_share_project_warnings)

add_subdirectory(native/core)
add_subdirectory(native/diagnostics)

if(WIN32)
    add_subdirectory(native/capture)
    add_subdirectory(native/renderer)
endif()

if(SCREEN_SHARE_ENABLE_LIVEKIT)
    if(NOT WIN32)
        message(FATAL_ERROR "The LiveKit media adapter currently supports Windows only")
    endif()

    include(ScreenShareLiveKit)
    screen_share_setup_livekit()
    add_subdirectory(native/media-livekit)
endif()

if(SCREEN_SHARE_BUILD_DESKTOP)
    if(NOT WIN32)
        message(FATAL_ERROR "The desktop client currently supports Windows only")
    endif()
    if(NOT TARGET ScreenShare::MediaLiveKit)
        message(FATAL_ERROR "The desktop client requires SCREEN_SHARE_ENABLE_LIVEKIT=ON")
    endif()

    find_package(Qt6 6.10.3 EXACT REQUIRED COMPONENTS Core Widgets)
    add_subdirectory(apps/desktop)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
