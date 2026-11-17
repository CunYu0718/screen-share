qt_add_executable(screen_share_desktop WIN32)

set(_screen_share_build_commit "unknown")
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _screen_share_git_result
        OUTPUT_VARIABLE _screen_share_git_output
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_screen_share_git_result EQUAL 0 AND _screen_share_git_output MATCHES "^[0-9a-fA-F]{40}$")
        set(_screen_share_build_commit "${_screen_share_git_output}")
    endif()
endif()

target_sources(
    screen_share_desktop
    PRIVATE
        src/main.cpp
        src/latency_marker_overlay.cpp
        src/latency_marker_overlay.hpp
        src/validation_report.cpp
        src/validation_report.hpp
)

target_link_libraries(
    screen_share_desktop
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        ScreenShare::Capture
        ScreenShare::Core
        ScreenShare::Diagnostics
        ScreenShare::MediaLiveKit
        ScreenShare::Renderer
        Psapi
        d3d11
        dxgi
        gdi32
        user32
        screen_share_project_options
        screen_share_project_warnings
)

target_compile_definitions(
    screen_share_desktop
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        SCREEN_SHARE_BUILD_COMMIT="${_screen_share_build_commit}"
        SCREEN_SHARE_LIVEKIT_SDK_VERSION="${SCREEN_SHARE_LIVEKIT_VERSION}"
        SCREEN_SHARE_PROJECT_VERSION="${PROJECT_VERSION}"
)

screen_share_copy_livekit_runtime(screen_share_desktop)

set_target_properties(
    screen_share_desktop
    PROPERTIES
        OUTPUT_NAME "ScreenShare"
        WIN32_EXECUTABLE TRUE
)
