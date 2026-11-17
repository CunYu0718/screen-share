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
