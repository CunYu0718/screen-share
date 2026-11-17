add_executable(screen_share_core_tests unit/core_tests.cpp)

target_link_libraries(
    screen_share_core_tests
    PRIVATE
        ScreenShare::Core
        ScreenShare::Diagnostics
        screen_share_project_options
        screen_share_project_warnings
)

add_test(NAME screen_share_core_tests COMMAND screen_share_core_tests)

if(TARGET ScreenShare::Capture)
    add_executable(screen_share_capture_tests unit/capture_source_tests.cpp)

    target_link_libraries(
        screen_share_capture_tests
        PRIVATE
            ScreenShare::Capture
            ScreenShare::Renderer
            screen_share_project_options
            screen_share_project_warnings
    )

    add_test(NAME screen_share_capture_tests COMMAND screen_share_capture_tests)
endif()

if(TARGET ScreenShare::MediaLiveKit)
    add_executable(screen_share_media_livekit_tests unit/media_livekit_tests.cpp)

    target_link_libraries(
        screen_share_media_livekit_tests
        PRIVATE
            ScreenShare::MediaLiveKit
            screen_share_project_options
            screen_share_project_warnings
    )

    screen_share_copy_livekit_runtime(screen_share_media_livekit_tests)
    add_test(NAME screen_share_media_livekit_tests COMMAND screen_share_media_livekit_tests)
endif()
