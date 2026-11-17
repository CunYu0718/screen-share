qt_add_executable(screen_share_desktop WIN32)

target_sources(screen_share_desktop PRIVATE src/main.cpp)

target_link_libraries(
    screen_share_desktop
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        ScreenShare::Core
        ScreenShare::Diagnostics
        screen_share_project_options
        screen_share_project_warnings
)

set_target_properties(
    screen_share_desktop
    PROPERTIES
        OUTPUT_NAME "ScreenShare"
        WIN32_EXECUTABLE TRUE
)
