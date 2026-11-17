add_library(screen_share_diagnostics STATIC)

target_sources(
    screen_share_diagnostics
    PRIVATE
        src/frame_rate_estimator.cpp
        src/frame_pipeline_stats.cpp
        src/visual_latency_marker.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/screen_share/diagnostics/frame_rate_estimator.hpp
            include/screen_share/diagnostics/frame_pipeline_stats.hpp
            include/screen_share/diagnostics/visual_latency_marker.hpp
)

target_link_libraries(
    screen_share_diagnostics
    PUBLIC
        screen_share_project_options
    PRIVATE
        screen_share_project_warnings
)

add_library(ScreenShare::Diagnostics ALIAS screen_share_diagnostics)
