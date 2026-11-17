#include "livekit_video_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace screen_share::media::detail {
namespace {

[[nodiscard]] std::uint64_t seconds_to_nanoseconds(const double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return 0;
    }
    constexpr double nanoseconds_per_second = 1'000'000'000.0;
    const auto nanoseconds = seconds * nanoseconds_per_second;
    if (nanoseconds >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(nanoseconds);
}

[[nodiscard]] double clamp_loss(const double value) noexcept {
    return std::clamp(value, 0.0, 1.0);
}

}  // namespace

OutboundVideoMetrics summarize_outbound_video(const livekit::SessionStats& stats) noexcept {
    OutboundVideoMetrics result;
    double total_encode_seconds = 0.0;
    double total_send_delay_seconds = 0.0;
    double loss_sum = 0.0;
    std::uint64_t loss_samples = 0;

    try {
        for (const auto& record : stats.publisher_stats) {
            if (const auto* outbound =
                    std::get_if<livekit::RtcOutboundRtpStats>(&record.stats);
                outbound != nullptr && outbound->stream.kind == "video") {
                result.has_video = true;
                result.frames_encoded += outbound->outbound.frames_encoded;
                result.frames_sent += outbound->outbound.frames_sent;
                result.packets_sent += outbound->sent.packets_sent;
                result.bytes_sent += outbound->sent.bytes_sent;
                total_encode_seconds += outbound->outbound.total_encode_time;
                total_send_delay_seconds += outbound->outbound.total_packet_send_delay;
                result.frames_per_second += outbound->outbound.frames_per_second;
                result.width = std::max(result.width, outbound->outbound.frame_width);
                result.height = std::max(result.height, outbound->outbound.frame_height);
                result.timestamp_ms = std::max(result.timestamp_ms, outbound->rtc.timestamp_ms);
            } else if (const auto* remote_inbound =
                           std::get_if<livekit::RtcRemoteInboundRtpStats>(&record.stats);
                       remote_inbound != nullptr && remote_inbound->stream.kind == "video") {
                loss_sum += clamp_loss(remote_inbound->remote_inbound.fraction_lost);
                ++loss_samples;
            }
        }
    } catch (...) {
        return {};
    }

    result.total_encode_duration_nanoseconds = seconds_to_nanoseconds(total_encode_seconds);
    result.total_send_delay_nanoseconds = seconds_to_nanoseconds(total_send_delay_seconds);
    result.packet_loss_ratio =
        loss_samples == 0U ? 0.0 : loss_sum / static_cast<double>(loss_samples);
    return result;
}

InboundVideoMetrics summarize_inbound_video(const livekit::SessionStats& stats) noexcept {
    InboundVideoMetrics result;
    double total_decode_seconds = 0.0;
    double total_receive_delay_seconds = 0.0;
    std::uint64_t packets_lost = 0;

    try {
        for (const auto& record : stats.subscriber_stats) {
            const auto* inbound = std::get_if<livekit::RtcInboundRtpStats>(&record.stats);
            if (inbound == nullptr || inbound->stream.kind != "video") {
                continue;
            }

            result.has_video = true;
            result.frames_received += inbound->inbound.frames_received;
            result.frames_decoded += inbound->inbound.frames_decoded;
            result.packets_received += inbound->received.packets_received;
            if (inbound->received.packets_lost > 0) {
                packets_lost += static_cast<std::uint64_t>(inbound->received.packets_lost);
            }
            result.bytes_received += inbound->inbound.bytes_received;
            total_decode_seconds += inbound->inbound.total_decode_time;
            total_receive_delay_seconds += inbound->inbound.jitter_buffer_delay;
            result.receive_delay_samples += inbound->inbound.jitter_buffer_emitted_count;
            result.frames_per_second += inbound->inbound.frames_per_second;
            result.width = std::max(result.width, inbound->inbound.frame_width);
            result.height = std::max(result.height, inbound->inbound.frame_height);
            result.timestamp_ms = std::max(result.timestamp_ms, inbound->rtc.timestamp_ms);
        }
    } catch (...) {
        return {};
    }

    result.total_decode_duration_nanoseconds = seconds_to_nanoseconds(total_decode_seconds);
    result.total_receive_delay_nanoseconds = seconds_to_nanoseconds(total_receive_delay_seconds);
    const auto packet_total = result.packets_received + packets_lost;
    result.packet_loss_ratio = packet_total == 0U
                                   ? 0.0
                                   : static_cast<double>(packets_lost) /
                                         static_cast<double>(packet_total);
    return result;
}

std::uint64_t bitrate_from_delta(
    const std::uint64_t previous_bytes,
    const std::int64_t previous_timestamp_ms,
    const std::uint64_t current_bytes,
    const std::int64_t current_timestamp_ms) noexcept {
    if (current_bytes < previous_bytes || current_timestamp_ms <= previous_timestamp_ms) {
        return 0;
    }

    const auto byte_delta = current_bytes - previous_bytes;
    const auto elapsed_ms = current_timestamp_ms - previous_timestamp_ms;
    constexpr std::uint64_t bits_per_byte = 8;
    constexpr std::uint64_t milliseconds_per_second = 1'000;
    if (byte_delta > std::numeric_limits<std::uint64_t>::max() /
                         (bits_per_byte * milliseconds_per_second)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return byte_delta * bits_per_byte * milliseconds_per_second /
           static_cast<std::uint64_t>(elapsed_ms);
}

}  // namespace screen_share::media::detail
