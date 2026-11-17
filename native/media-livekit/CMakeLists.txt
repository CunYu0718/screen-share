add_library(screen_share_media_livekit STATIC)

target_sources(
    screen_share_media_livekit
    PRIVATE
        src/d3d11_bgra_frame_reader.cpp
        src/livekit_screen_publisher.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/screen_share/media/d3d11_bgra_frame_reader.hpp
            include/screen_share/media/livekit_screen_publisher.hpp
            include/screen_share/media/raw_video_frame.hpp
)

target_compile_definitions(
    screen_share_media_livekit
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_link_libraries(
    screen_share_media_livekit
    PUBLIC
        ScreenShare::Capture
        screen_share_project_options
    PRIVATE
        ScreenShare::LiveKitSDK
        screen_share_project_warnings
        d3d11
        dxgi
)

add_library(ScreenShare::MediaLiveKit ALIAS screen_share_media_livekit)
