#pragma once

#include "screen_share/capture/wgc_capture_session.hpp"
#include "screen_share/media/raw_video_frame.hpp"

#include <cstdint>
#include <memory>

namespace screen_share::media {

enum class D3D11ReadbackResult : std::uint8_t {
    Read,
    InvalidFrame,
    UnsupportedFormat,
    DeviceUnavailable,
    Failed,
};

class D3D11BgraFrameReader final {
public:
    D3D11BgraFrameReader();
    ~D3D11BgraFrameReader();

    D3D11BgraFrameReader(const D3D11BgraFrameReader&) = delete;
    D3D11BgraFrameReader& operator=(const D3D11BgraFrameReader&) = delete;
    D3D11BgraFrameReader(D3D11BgraFrameReader&&) = delete;
    D3D11BgraFrameReader& operator=(D3D11BgraFrameReader&&) = delete;

    [[nodiscard]] D3D11ReadbackResult read(
        const screen_share::capture::D3D11CaptureFrameView& source,
        BgraVideoFrame& destination) noexcept;
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace screen_share::media
