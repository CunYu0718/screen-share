add_library(screen_share_capture STATIC)

target_sources(
    screen_share_capture
    PRIVATE
        src/capture_source.cpp
        src/capture_source_catalog_win.cpp
        src/wgc_capture_session.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/screen_share/capture/capture_source.hpp
            include/screen_share/capture/wgc_capture_session.hpp
)

target_compile_definitions(
    screen_share_capture
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        WINRT_LEAN_AND_MEAN
)

target_link_libraries(
    screen_share_capture
    PUBLIC
        screen_share_project_options
    PRIVATE
        screen_share_project_warnings
        d3d11
        dwmapi
        dxgi
        windowsapp
)

add_library(ScreenShare::Capture ALIAS screen_share_capture)
