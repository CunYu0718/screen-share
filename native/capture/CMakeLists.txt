add_library(screen_share_capture STATIC)

target_sources(
    screen_share_capture
    PRIVATE
        src/capture_source.cpp
        src/capture_source_catalog_win.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/screen_share/capture/capture_source.hpp
)

target_compile_definitions(
    screen_share_capture
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(
    screen_share_capture
    PUBLIC
        screen_share_project_options
    PRIVATE
        screen_share_project_warnings
        dwmapi
)

add_library(ScreenShare::Capture ALIAS screen_share_capture)
