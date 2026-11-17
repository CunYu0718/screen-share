#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace screen_share::media {

struct BgraVideoFrame {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::int64_t timestamp_us{0};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool valid() const noexcept {
        if (width == 0U || height == 0U) {
            return false;
        }

        constexpr auto bytes_per_pixel = std::size_t{4};
        const auto maximum = std::numeric_limits<std::size_t>::max();
        if (static_cast<std::size_t>(width) > maximum / bytes_per_pixel) {
            return false;
        }
        const auto row_bytes = static_cast<std::size_t>(width) * bytes_per_pixel;
        if (static_cast<std::size_t>(height) > maximum / row_bytes) {
            return false;
        }
        return pixels.size() == row_bytes * static_cast<std::size_t>(height);
    }
};

}  // namespace screen_share::media
