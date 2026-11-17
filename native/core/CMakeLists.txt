add_library(screen_share_core STATIC)

target_sources(
    screen_share_core
    PRIVATE
        src/quality_profile.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/screen_share/core/quality_profile.hpp
            include/screen_share/core/session_policy.hpp
)

target_link_libraries(
    screen_share_core
    PUBLIC
        screen_share_project_options
    PRIVATE
        screen_share_project_warnings
)

add_library(ScreenShare::Core ALIAS screen_share_core)
