include_guard(GLOBAL)

set(SCREEN_SHARE_LIVEKIT_VERSION "1.13.1")
set(
    SCREEN_SHARE_LIVEKIT_WINDOWS_X64_SHA256
    "6696ee87c78b958bdbf70e5eb39bf85e1c7528ca2a707e6c13b042c8e4360b9d"
)

set(
    SCREEN_SHARE_LIVEKIT_SDK_ROOT
    ""
    CACHE PATH
    "Optional path to an extracted LiveKit C++ SDK; skips the pinned download"
)

function(screen_share_setup_livekit)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The pinned LiveKit package currently supports Windows x64 only")
    endif()

    if(SCREEN_SHARE_LIVEKIT_SDK_ROOT)
        set(_livekit_root "${SCREEN_SHARE_LIVEKIT_SDK_ROOT}")
    else()
        set(_archive_name "livekit-sdk-windows-x64-${SCREEN_SHARE_LIVEKIT_VERSION}.zip")
        set(_download_dir "${CMAKE_BINARY_DIR}/_downloads")
        set(_sdk_dir "${CMAKE_BINARY_DIR}/_deps/livekit-sdk")
        set(_archive_path "${_download_dir}/${_archive_name}")
        set(_livekit_root "${_sdk_dir}/livekit-sdk-windows-x64-${SCREEN_SHARE_LIVEKIT_VERSION}")
        set(
            _download_url
            "https://github.com/livekit/client-sdk-cpp/releases/download/v${SCREEN_SHARE_LIVEKIT_VERSION}/${_archive_name}"
        )

        if(NOT EXISTS "${_livekit_root}/lib/cmake/LiveKit/LiveKitConfig.cmake")
            file(MAKE_DIRECTORY "${_download_dir}" "${_sdk_dir}")
            message(STATUS "Downloading pinned LiveKit C++ SDK ${SCREEN_SHARE_LIVEKIT_VERSION}")
            file(
                DOWNLOAD
                "${_download_url}"
                "${_archive_path}"
                EXPECTED_HASH "SHA256=${SCREEN_SHARE_LIVEKIT_WINDOWS_X64_SHA256}"
                TLS_VERIFY ON
                SHOW_PROGRESS
                STATUS _download_status
            )
            list(GET _download_status 0 _download_code)
            list(GET _download_status 1 _download_message)
            if(NOT _download_code EQUAL 0)
                message(
                    FATAL_ERROR
                    "LiveKit SDK download failed (${_download_code}): ${_download_message}\n${_download_url}"
                )
            endif()

            file(REMOVE_RECURSE "${_livekit_root}")
            file(ARCHIVE_EXTRACT INPUT "${_archive_path}" DESTINATION "${_sdk_dir}")
        endif()
    endif()

    set(_livekit_config_dir "${_livekit_root}/lib/cmake/LiveKit")
    if(NOT EXISTS "${_livekit_config_dir}/LiveKitConfig.cmake")
        message(FATAL_ERROR "Invalid LiveKit SDK root: ${_livekit_root}")
    endif()

    find_package(LiveKit CONFIG REQUIRED PATHS "${_livekit_config_dir}" NO_DEFAULT_PATH)

    if(TARGET LiveKit::livekit)
        set(_livekit_target LiveKit::livekit)
    elseif(TARGET livekit)
        set(_livekit_target livekit)
    else()
        message(FATAL_ERROR "LiveKit package did not export LiveKit::livekit or livekit")
    endif()

    add_library(screen_share_livekit_sdk INTERFACE)
    target_link_libraries(screen_share_livekit_sdk INTERFACE ${_livekit_target})
    add_library(ScreenShare::LiveKitSDK ALIAS screen_share_livekit_sdk)

    set(SCREEN_SHARE_LIVEKIT_RESOLVED_ROOT "${_livekit_root}" CACHE INTERNAL "")
endfunction()

function(screen_share_copy_livekit_runtime target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Unknown target passed to screen_share_copy_livekit_runtime: ${target_name}")
    endif()

    foreach(_runtime_dll IN ITEMS livekit.dll livekit_ffi.dll)
        set(_runtime_path "${SCREEN_SHARE_LIVEKIT_RESOLVED_ROOT}/bin/${_runtime_dll}")
        if(NOT EXISTS "${_runtime_path}")
            message(FATAL_ERROR "LiveKit runtime DLL is missing: ${_runtime_path}")
        endif()
        add_custom_command(
            TARGET ${target_name}
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E copy_if_different
                "${_runtime_path}"
                "$<TARGET_FILE_DIR:${target_name}>"
        )
    endforeach()
endfunction()
