#pragma once

#include <livekit/stats.h>

#include <cstdint>

namespace screen_share::media::detail {

struct OutboundVideoMetrics {
    std::uint64_t frames_encoded{0};
    std::uint64_t frames_sent{0};
    std::uint64_t packets_sent{0};
    std::uint64_t bytes_sent{0};
    std::uint64_t total_encode_duration_nanoseconds{0};
    std::uint64_t total_send_delay_nanoseconds{0};
    std::int64_t timestamp_ms{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    double frames_per_second{0.0};
    double packet_loss_ratio{0.0};
    bool has_video{false};
};

struct InboundVideoMetrics {
    std::uint64_t frames_received{0};
    std::uint64_t frames_decoded{0};
    std::uint64_t packets_received{0};
    std::uint64_t bytes_received{0};
    std::uint64_t total_receive_delay_nanoseconds{0};
    std::uint64_t receive_delay_samples{0};
    std::uint64_t total_decode_duration_nanoseconds{0};
    std::int64_t timestamp_ms{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    double frames_per_second{0.0};
    double packet_loss_ratio{0.0};
    bool has_video{false};
};

[[nodiscard]] OutboundVideoMetrics summarize_outbound_video(
    const livekit::SessionStats& stats) noexcept;
[[nodiscard]] InboundVideoMetrics summarize_inbound_video(
    const livekit::SessionStats& stats) noexcept;
[[nodiscard]] std::uint64_t bitrate_from_delta(
    std::uint64_t previous_bytes,
    std::int64_t previous_timestamp_ms,
    std::uint64_t current_bytes,
    std::int64_t current_timestamp_ms) noexcept;

}  // namespace screen_share::media::detail
