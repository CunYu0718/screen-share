#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace screen_share::capture {

enum class CaptureSourceKind : std::uint8_t {
    Monitor,
    Window,
};

struct CaptureSourceId {
    CaptureSourceKind kind;
    std::uintptr_t native_handle;

    friend constexpr bool operator==(const CaptureSourceId&, const CaptureSourceId&) = default;
};

struct CaptureSourceBounds {
    std::int32_t x;
    std::int32_t y;
    std::uint32_t width;
    std::uint32_t height;

    friend constexpr bool operator==(const CaptureSourceBounds&, const CaptureSourceBounds&) = default;
};

struct CaptureSource {
    CaptureSourceId id;
    std::wstring name;
    CaptureSourceBounds bounds;
    bool primary{false};
    bool minimized{false};
};

enum class CaptureSourceEnumerationStatus : std::uint8_t {
    Complete,
    MonitorEnumerationFailed,
    WindowEnumerationFailed,
    Failed,
};

struct CaptureSourceSnapshot {
    std::vector<CaptureSource> sources;
    CaptureSourceEnumerationStatus status{CaptureSourceEnumerationStatus::Complete};

    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] const CaptureSource* find(CaptureSourceId id) const noexcept;
};

class CaptureSourceCatalog final {
public:
    [[nodiscard]] static CaptureSourceSnapshot enumerate();
};

}  // namespace screen_share::capture
