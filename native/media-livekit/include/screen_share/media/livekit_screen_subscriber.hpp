#pragma once

#include "screen_share/media/livekit_url.hpp"
#include "screen_share/media/raw_video_frame.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace screen_share::media {

struct LiveKitSubscribeConfig {
    std::string server_url;
    std::string access_token;
    std::string publisher_identity;
    std::string track_name{"screen-share"};
};

enum class LiveKitSubscribeStartResult : std::uint8_t {
    Starting,
    AlreadyActive,
    InvalidConfiguration,
    FailedToStart,
};

enum class LiveKitSubscribeState : std::uint8_t {
    Idle,
    Connecting,
    Watching,
    Failed,
};

enum class LiveKitSubscribeError : std::uint8_t {
    None,
    ConnectionFailed,
    CallbackRegistrationFailed,
};

struct LiveKitSubscribeSnapshot {
    LiveKitSubscribeState state{LiveKitSubscribeState::Idle};
    LiveKitSubscribeError error{LiveKitSubscribeError::None};
    std::uint64_t callback_frames{0};
    std::uint64_t delivered_frames{0};
    std::uint64_t dropped_frames{0};
    std::uint64_t conversion_failures{0};
    std::uint64_t received_frames{0};
    std::uint64_t decoded_frames{0};
    std::uint64_t packets_received{0};
    std::uint64_t bytes_received{0};
    std::uint64_t bitrate_bps{0};
    std::uint64_t total_receive_delay_nanoseconds{0};
    std::uint64_t receive_delay_samples{0};
    std::uint64_t total_decode_duration_nanoseconds{0};
    std::uint64_t stats_samples{0};
    std::uint64_t stats_failures{0};
    double reported_frames_per_second{0.0};
    double packet_loss_ratio{0.0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

class LiveKitScreenSubscriber final {
public:
    LiveKitScreenSubscriber();
    ~LiveKitScreenSubscriber();

    LiveKitScreenSubscriber(const LiveKitScreenSubscriber&) = delete;
    LiveKitScreenSubscriber& operator=(const LiveKitScreenSubscriber&) = delete;
    LiveKitScreenSubscriber(LiveKitScreenSubscriber&&) = delete;
    LiveKitScreenSubscriber& operator=(LiveKitScreenSubscriber&&) = delete;

    [[nodiscard]] LiveKitSubscribeStartResult start(
        LiveKitSubscribeConfig configuration) noexcept;
    [[nodiscard]] bool take_latest(BgraVideoFrame& frame) noexcept;
    void stop() noexcept;
    [[nodiscard]] LiveKitSubscribeSnapshot snapshot() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace screen_share::media
