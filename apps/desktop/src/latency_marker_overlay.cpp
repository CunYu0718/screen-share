#include "latency_marker_overlay.hpp"

#include "screen_share/diagnostics/visual_latency_marker.hpp"

#include <Windows.h>

#include <array>
#include <cstddef>

namespace screen_share::desktop {
namespace {

constexpr wchar_t marker_window_class[] = L"ScreenShareValidationLatencyMarker";
constexpr int marker_width = static_cast<int>(
    diagnostics::latency_marker_columns * diagnostics::latency_marker_cell_size);
constexpr int marker_height = static_cast<int>(
    diagnostics::latency_marker_rows * diagnostics::latency_marker_cell_size);

}  // namespace

struct LatencyMarkerOverlay::Impl final {
    HWND window{nullptr};
    diagnostics::VisualLatencyMarkerBits bits{};

    ~Impl() {
        if (window != nullptr) {
            DestroyWindow(window);
            window = nullptr;
        }
    }

    [[nodiscard]] static LRESULT CALLBACK window_proc(
        const HWND window_handle,
        const UINT message,
        const WPARAM word_parameter,
        const LPARAM long_parameter) noexcept {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(long_parameter);
            SetWindowLongPtrW(
                window_handle,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(window_handle, GWLP_USERDATA));

        switch (message) {
            case WM_PAINT:
                if (self != nullptr) {
                    self->paint(window_handle);
                    return 0;
                }
                break;
            case WM_ERASEBKGND:
                return 1;
            case WM_MOUSEACTIVATE:
                return MA_NOACTIVATE;
            case WM_NCHITTEST:
                return HTTRANSPARENT;
            default:
                break;
        }
        return DefWindowProcW(window_handle, message, word_parameter, long_parameter);
    }

    [[nodiscard]] static bool register_window_class() noexcept {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &Impl::window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = marker_window_class;
        if (RegisterClassExW(&window_class) != 0) {
            return true;
        }
        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    [[nodiscard]] bool ensure_window() noexcept {
        if (window != nullptr) {
            return true;
        }
        if (!register_window_class()) {
            return false;
        }
        window = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            marker_window_class,
            L"",
            WS_POPUP,
            0,
            0,
            marker_width,
            marker_height,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            this);
        return window != nullptr;
    }

    void paint(const HWND window_handle) const noexcept {
        PAINTSTRUCT paint_state{};
        const HDC target = BeginPaint(window_handle, &paint_state);
        if (target == nullptr) {
            return;
        }

        const HDC buffer = CreateCompatibleDC(target);
        const HBITMAP bitmap = CreateCompatibleBitmap(target, marker_width, marker_height);
        if (buffer == nullptr || bitmap == nullptr) {
            if (bitmap != nullptr) {
                DeleteObject(bitmap);
            }
            if (buffer != nullptr) {
                DeleteDC(buffer);
            }
            EndPaint(window_handle, &paint_state);
            return;
        }

        const HGDIOBJ previous_bitmap = SelectObject(buffer, bitmap);
        const HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        const HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        RECT background{0, 0, marker_width, marker_height};
        FillRect(buffer, &background, black);

        for (std::size_t index = 0; index < bits.size(); ++index) {
            const auto column = static_cast<int>(index % diagnostics::latency_marker_columns);
            const auto row = static_cast<int>(index / diagnostics::latency_marker_columns);
            const int cell_size = static_cast<int>(diagnostics::latency_marker_cell_size);
            RECT cell{
                column * cell_size,
                row * cell_size,
                (column + 1) * cell_size,
                (row + 1) * cell_size,
            };
            FillRect(buffer, &cell, bits[index] ? white : black);
        }

        BitBlt(target, 0, 0, marker_width, marker_height, buffer, 0, 0, SRCCOPY);
        DeleteObject(black);
        DeleteObject(white);
        SelectObject(buffer, previous_bitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window_handle, &paint_state);
    }

    [[nodiscard]] bool show_on_monitor(const std::uintptr_t monitor_handle) noexcept {
        if (monitor_handle == 0U || !ensure_window()) {
            return false;
        }
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(reinterpret_cast<HMONITOR>(monitor_handle), &monitor) == FALSE) {
            return false;
        }
        const int x = monitor.rcMonitor.left +
                      static_cast<int>(diagnostics::latency_marker_origin_x);
        const int y = monitor.rcMonitor.top +
                      static_cast<int>(diagnostics::latency_marker_origin_y);
        return SetWindowPos(
                   window,
                   HWND_TOPMOST,
                   x,
                   y,
                   marker_width,
                   marker_height,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE;
    }
};

LatencyMarkerOverlay::LatencyMarkerOverlay() : implementation_(std::make_unique<Impl>()) {
}

LatencyMarkerOverlay::~LatencyMarkerOverlay() = default;

bool LatencyMarkerOverlay::show_on_monitor(const std::uintptr_t monitor_handle) noexcept {
    return implementation_ && implementation_->show_on_monitor(monitor_handle);
}

void LatencyMarkerOverlay::update_timestamp(
    const std::uint64_t utc_timestamp_milliseconds) noexcept {
    if (!implementation_ || implementation_->window == nullptr || !visible()) {
        return;
    }
    implementation_->bits =
        diagnostics::make_visual_latency_marker(utc_timestamp_milliseconds);
    InvalidateRect(implementation_->window, nullptr, FALSE);
    UpdateWindow(implementation_->window);
}

void LatencyMarkerOverlay::hide() noexcept {
    if (implementation_ && implementation_->window != nullptr) {
        ShowWindow(implementation_->window, SW_HIDE);
    }
}

bool LatencyMarkerOverlay::visible() const noexcept {
    return implementation_ && implementation_->window != nullptr &&
           IsWindowVisible(implementation_->window) != FALSE;
}

}  // namespace screen_share::desktop
