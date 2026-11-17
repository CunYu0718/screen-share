#include "screen_share/capture/capture_source.hpp"

#include <dwmapi.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace screen_share::capture {
namespace {

struct EnumerationContext {
    std::vector<CaptureSource>* sources;
    DWORD current_process_id;
    std::exception_ptr exception;
};

[[nodiscard]] CaptureSourceBounds to_bounds(const RECT& rectangle) noexcept {
    const auto width = static_cast<std::int64_t>(rectangle.right) - static_cast<std::int64_t>(rectangle.left);
    const auto height = static_cast<std::int64_t>(rectangle.bottom) - static_cast<std::int64_t>(rectangle.top);

    return {
        .x = static_cast<std::int32_t>(rectangle.left),
        .y = static_cast<std::int32_t>(rectangle.top),
        .width = width > 0 ? static_cast<std::uint32_t>(width) : 0U,
        .height = height > 0 ? static_cast<std::uint32_t>(height) : 0U,
    };
}

BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM context_value) {
    auto& context = *reinterpret_cast<EnumerationContext*>(context_value);

    try {
        MONITORINFOEXW monitor_info{};
        monitor_info.cbSize = static_cast<DWORD>(sizeof(monitor_info));
        if (GetMonitorInfoW(monitor, &monitor_info) == FALSE) {
            return TRUE;
        }

        context.sources->push_back({
            .id = {
                .kind = CaptureSourceKind::Monitor,
                .native_handle = reinterpret_cast<std::uintptr_t>(monitor),
            },
            .name = monitor_info.szDevice,
            .bounds = to_bounds(monitor_info.rcMonitor),
            .primary = (monitor_info.dwFlags & MONITORINFOF_PRIMARY) != 0U,
            .minimized = false,
        });
    } catch (...) {
        context.exception = std::current_exception();
        return FALSE;
    }

    return TRUE;
}

[[nodiscard]] bool is_capturable_window(const HWND window, const DWORD current_process_id) noexcept {
    if (window == GetShellWindow() || IsWindowVisible(window) == FALSE) {
        return false;
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == current_process_id) {
        return false;
    }

    const auto extended_style = static_cast<DWORD_PTR>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    if ((extended_style & WS_EX_TOOLWINDOW) != 0U) {
        return false;
    }

    DWORD cloaked = 0;
    const auto cloak_result = DwmGetWindowAttribute(
        window, DWMWA_CLOAKED, &cloaked, static_cast<DWORD>(sizeof(cloaked)));
    if (SUCCEEDED(cloak_result) && cloaked != 0U) {
        return false;
    }

    return GetWindowTextLengthW(window) > 0;
}

[[nodiscard]] RECT window_bounds(const HWND window, const bool minimized) noexcept {
    RECT bounds{};
    if (!minimized) {
        const auto frame_result = DwmGetWindowAttribute(
            window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, static_cast<DWORD>(sizeof(bounds)));
        if (SUCCEEDED(frame_result)) {
            return bounds;
        }
    }

    if (GetWindowRect(window, &bounds) == FALSE) {
        return {};
    }

    return bounds;
}

BOOL CALLBACK collect_window(HWND window, LPARAM context_value) {
    auto& context = *reinterpret_cast<EnumerationContext*>(context_value);
    if (!is_capturable_window(window, context.current_process_id)) {
        return TRUE;
    }

    try {
        const int title_length = GetWindowTextLengthW(window);
        std::vector<wchar_t> title_buffer(static_cast<std::size_t>(title_length) + 1U, L'\0');
        const int copied_length = GetWindowTextW(window, title_buffer.data(), title_length + 1);
        if (copied_length <= 0) {
            return TRUE;
        }

        const bool minimized = IsIconic(window) != FALSE;
        auto bounds = window_bounds(window, minimized);
        if (minimized) {
            bounds.width = 0;
            bounds.height = 0;
        } else if (bounds.width == 0U || bounds.height == 0U) {
            return TRUE;
        }

        context.sources->push_back({
            .id = {
                .kind = CaptureSourceKind::Window,
                .native_handle = reinterpret_cast<std::uintptr_t>(window),
            },
            .name = std::wstring(title_buffer.data(), static_cast<std::size_t>(copied_length)),
            .bounds = bounds,
            .primary = false,
            .minimized = minimized,
        });
    } catch (...) {
        context.exception = std::current_exception();
        return FALSE;
    }

    return TRUE;
}

[[nodiscard]] CaptureSourceEnumerationStatus enumeration_status(
    const bool monitors_succeeded, const bool windows_succeeded) noexcept {
    if (monitors_succeeded && windows_succeeded) {
        return CaptureSourceEnumerationStatus::Complete;
    }
    if (windows_succeeded) {
        return CaptureSourceEnumerationStatus::MonitorEnumerationFailed;
    }
    if (monitors_succeeded) {
        return CaptureSourceEnumerationStatus::WindowEnumerationFailed;
    }
    return CaptureSourceEnumerationStatus::Failed;
}

}  // namespace

CaptureSourceSnapshot CaptureSourceCatalog::enumerate() {
    CaptureSourceSnapshot snapshot;
    EnumerationContext context{
        .sources = &snapshot.sources,
        .current_process_id = GetCurrentProcessId(),
        .exception = nullptr,
    };

    const bool monitors_succeeded = EnumDisplayMonitors(nullptr, nullptr, collect_monitor,
                                                        reinterpret_cast<LPARAM>(&context)) != FALSE;
    if (context.exception != nullptr) {
        std::rethrow_exception(context.exception);
    }

    const bool windows_succeeded =
        EnumWindows(collect_window, reinterpret_cast<LPARAM>(&context)) != FALSE;
    if (context.exception != nullptr) {
        std::rethrow_exception(context.exception);
    }

    snapshot.status = enumeration_status(monitors_succeeded, windows_succeeded);
    std::sort(snapshot.sources.begin(), snapshot.sources.end(), [](const CaptureSource& left,
                                                                   const CaptureSource& right) {
        if (left.id.kind != right.id.kind) {
            return left.id.kind == CaptureSourceKind::Monitor;
        }
        if (left.id.kind == CaptureSourceKind::Monitor && left.primary != right.primary) {
            return left.primary;
        }
        return left.name < right.name;
    });

    return snapshot;
}

}  // namespace screen_share::capture
