qt_add_executable(screen_share_desktop WIN32)

target_sources(
    screen_share_desktop
    PRIVATE
        src/main.cpp
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
        screen_share_project_options
        screen_share_project_warnings
)

screen_share_copy_livekit_runtime(screen_share_desktop)

set_target_properties(
    screen_share_desktop
    PROPERTIES
        OUTPUT_NAME "ScreenShare"
        WIN32_EXECUTABLE TRUE
)
