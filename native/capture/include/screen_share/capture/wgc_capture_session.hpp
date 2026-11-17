#pragma once

#include "screen_share/capture/capture_source.hpp"

#include <cstdint>
#include <functional>
#include <memory>

struct ID3D11Texture2D;

namespace screen_share::capture {

struct D3D11CaptureFrameView {
    // Borrowed GPU texture. It is valid only for the duration of the frame callback.
    ID3D11Texture2D* texture;
    std::uint32_t width;
    std::uint32_t height;
    std::int64_t system_relative_time_100ns;
};

using D3D11CaptureFrameHandler = std::function<void(const D3D11CaptureFrameView&)>;

enum class WgcCaptureStartResult : std::uint8_t {
    Started,
    AlreadyRunning,
    Unsupported,
    InvalidSource,
    DeviceCreationFailed,
    CaptureItemCreationFailed,
    SessionCreationFailed,
};

class WgcCaptureSession final {
public:
    WgcCaptureSession();
    ~WgcCaptureSession();

    WgcCaptureSession(const WgcCaptureSession&) = delete;
    WgcCaptureSession& operator=(const WgcCaptureSession&) = delete;
    WgcCaptureSession(WgcCaptureSession&&) = delete;
    WgcCaptureSession& operator=(WgcCaptureSession&&) = delete;

    [[nodiscard]] static bool is_supported() noexcept;
    [[nodiscard]] WgcCaptureStartResult start(CaptureSourceId source,
                                               D3D11CaptureFrameHandler frame_handler);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> implementation_;
};

}  // namespace screen_share::capture
