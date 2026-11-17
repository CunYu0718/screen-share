#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace screen_share::diagnostics {

inline constexpr std::uint32_t latency_marker_columns = 16;
inline constexpr std::uint32_t latency_marker_rows = 6;
inline constexpr std::uint32_t latency_marker_cell_size = 8;
inline constexpr std::uint32_t latency_marker_origin_x = 8;
inline constexpr std::uint32_t latency_marker_origin_y = 8;
inline constexpr std::size_t latency_marker_bit_count =
    latency_marker_columns * latency_marker_rows;

using VisualLatencyMarkerBits = std::array<bool, latency_marker_bit_count>;

[[nodiscard]] VisualLatencyMarkerBits make_visual_latency_marker(
    std::uint64_t utc_timestamp_milliseconds) noexcept;

[[nodiscard]] std::optional<std::uint64_t> decode_visual_latency_marker_bgra(
    std::span<const std::uint8_t> pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t row_pitch) noexcept;

[[nodiscard]] std::uint64_t utc_timestamp_milliseconds() noexcept;

}  // namespace screen_share::diagnostics
