add_library(screen_share_renderer STATIC)

target_sources(
    screen_share_renderer
    PRIVATE
        src/d3d11_preview_renderer.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/screen_share/renderer/d3d11_preview_renderer.hpp
)

target_compile_definitions(
    screen_share_renderer
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(
    screen_share_renderer
    PUBLIC
        ScreenShare::Capture
        screen_share_project_options
    PRIVATE
        screen_share_project_warnings
        d3d11
        dxgi
        user32
)

add_library(ScreenShare::Renderer ALIAS screen_share_renderer)
