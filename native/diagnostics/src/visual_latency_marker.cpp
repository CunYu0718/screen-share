#include "screen_share/diagnostics/visual_latency_marker.hpp"

#include <array>
#include <chrono>
#include <limits>

namespace screen_share::diagnostics {
namespace {

constexpr std::uint16_t marker_magic = 0x5353;
constexpr std::uint8_t marker_version = 1;
constexpr std::size_t payload_size = latency_marker_bit_count / 8;
constexpr std::uint64_t timestamp_mask = (std::uint64_t{1} << 48U) - 1U;

[[nodiscard]] std::uint16_t crc16_ccitt(
    const std::span<const std::uint8_t> bytes) noexcept {
    std::uint16_t crc = 0xFFFFU;
    for (const std::uint8_t byte : bytes) {
        crc ^= static_cast<std::uint16_t>(byte) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0U
                      ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                      : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

[[nodiscard]] std::array<std::uint8_t, payload_size> marker_payload(
    const std::uint64_t timestamp_milliseconds) noexcept {
    std::array<std::uint8_t, payload_size> payload{};
    payload[0] = static_cast<std::uint8_t>(marker_magic >> 8U);
    payload[1] = static_cast<std::uint8_t>(marker_magic & 0xFFU);
    payload[2] = marker_version;
    const std::uint64_t timestamp = timestamp_milliseconds & timestamp_mask;
    for (std::size_t index = 0; index < 6; ++index) {
        const auto shift = static_cast<unsigned int>((5U - index) * 8U);
        payload[3U + index] = static_cast<std::uint8_t>((timestamp >> shift) & 0xFFU);
    }
    payload[9] = 0;
    const auto crc = crc16_ccitt(
        std::span<const std::uint8_t>(payload.data(), payload.size()).first(10));
    payload[10] = static_cast<std::uint8_t>(crc >> 8U);
    payload[11] = static_cast<std::uint8_t>(crc & 0xFFU);
    return payload;
}

}  // namespace

VisualLatencyMarkerBits make_visual_latency_marker(
    const std::uint64_t utc_timestamp_milliseconds) noexcept {
    const auto payload = marker_payload(utc_timestamp_milliseconds);
    VisualLatencyMarkerBits bits{};
    for (std::size_t index = 0; index < bits.size(); ++index) {
        const auto byte = payload[index / 8U];
        const auto shift = static_cast<unsigned int>(7U - (index % 8U));
        bits[index] = ((byte >> shift) & 1U) != 0U;
    }
    return bits;
}

std::optional<std::uint64_t> decode_visual_latency_marker_bgra(
    const std::span<const std::uint8_t> pixels,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t row_pitch) noexcept {
    constexpr std::uint32_t marker_width = latency_marker_columns * latency_marker_cell_size;
    constexpr std::uint32_t marker_height = latency_marker_rows * latency_marker_cell_size;
    if (width < latency_marker_origin_x + marker_width ||
        height < latency_marker_origin_y + marker_height ||
        width > std::numeric_limits<std::uint32_t>::max() / 4U ||
        row_pitch < width * 4U) {
        return std::nullopt;
    }
    const std::uint64_t required_size =
        static_cast<std::uint64_t>(height - 1U) * row_pitch +
        static_cast<std::uint64_t>(width) * 4U;
    if (required_size > pixels.size()) {
        return std::nullopt;
    }

    std::array<std::uint8_t, payload_size> payload{};
    for (std::size_t index = 0; index < latency_marker_bit_count; ++index) {
        const auto column = static_cast<std::uint32_t>(index % latency_marker_columns);
        const auto row = static_cast<std::uint32_t>(index / latency_marker_columns);
        const auto center_x = latency_marker_origin_x + column * latency_marker_cell_size +
                              latency_marker_cell_size / 2U;
        const auto center_y = latency_marker_origin_y + row * latency_marker_cell_size +
                              latency_marker_cell_size / 2U;
        std::uint32_t luminance_total = 0;
        for (std::uint32_t y = center_y - 1U; y <= center_y + 1U; ++y) {
            for (std::uint32_t x = center_x - 1U; x <= center_x + 1U; ++x) {
                const auto offset = static_cast<std::size_t>(y) * row_pitch +
                                    static_cast<std::size_t>(x) * 4U;
                luminance_total += pixels[offset];
                luminance_total += pixels[offset + 1U];
                luminance_total += pixels[offset + 2U];
            }
        }
        const bool value = luminance_total >= 9U * 3U * 128U;
        if (value) {
            const auto shift = static_cast<unsigned int>(7U - (index % 8U));
            payload[index / 8U] |= static_cast<std::uint8_t>(1U << shift);
        }
    }

    if (payload[0] != static_cast<std::uint8_t>(marker_magic >> 8U) ||
        payload[1] != static_cast<std::uint8_t>(marker_magic & 0xFFU) ||
        payload[2] != marker_version || payload[9] != 0U) {
        return std::nullopt;
    }
    const auto expected_crc = crc16_ccitt(
        std::span<const std::uint8_t>(payload.data(), payload.size()).first(10));
    const auto actual_crc = static_cast<std::uint16_t>(payload[10]) << 8U |
                            static_cast<std::uint16_t>(payload[11]);
    if (actual_crc != expected_crc) {
        return std::nullopt;
    }

    std::uint64_t timestamp = 0;
    for (std::size_t index = 0; index < 6; ++index) {
        timestamp = (timestamp << 8U) | payload[3U + index];
    }
    return timestamp;
}

std::uint64_t utc_timestamp_milliseconds() noexcept {
    const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

}  // namespace screen_share::diagnostics
