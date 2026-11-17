#pragma once

#include "screen_share/media/livekit_url.hpp"
#include "screen_share/media/raw_video_frame.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace screen_share::media {

struct LiveKitPublishConfig {
    std::string server_url;
    std::string access_token;
    std::string track_name{"screen-share"};
};

enum class LiveKitPublishStartResult : std::uint8_t {
    Starting,
    AlreadyActive,
    InvalidConfiguration,
    FailedToStart,
};

enum class LiveKitPublishState : std::uint8_t {
    Idle,
    Connecting,
    Publishing,
    Failed,
};

enum class LiveKitPublishError : std::uint8_t {
    None,
    ConnectionFailed,
    ParticipantUnavailable,
    TrackPublicationFailed,
    FramePublicationFailed,
};

struct LiveKitPublishSnapshot {
    LiveKitPublishState state{LiveKitPublishState::Idle};
    LiveKitPublishError error{LiveKitPublishError::None};
    std::uint64_t accepted_frames{0};
    std::uint64_t published_frames{0};
    std::uint64_t dropped_frames{0};
    std::uint64_t encoded_frames{0};
    std::uint64_t sent_frames{0};
    std::uint64_t packets_sent{0};
    std::uint64_t bytes_sent{0};
    std::uint64_t bitrate_bps{0};
    std::uint64_t total_encode_duration_nanoseconds{0};
    std::uint64_t total_send_delay_nanoseconds{0};
    std::uint64_t stats_samples{0};
    std::uint64_t stats_failures{0};
    double reported_frames_per_second{0.0};
    double packet_loss_ratio{0.0};
    std::uint32_t width{0};
    std::uint32_t height{0};
};

class LiveKitScreenPublisher final {
public:
    LiveKitScreenPublisher();
    ~LiveKitScreenPublisher();

    LiveKitScreenPublisher(const LiveKitScreenPublisher&) = delete;
    LiveKitScreenPublisher& operator=(const LiveKitScreenPublisher&) = delete;
    LiveKitScreenPublisher(LiveKitScreenPublisher&&) = delete;
    LiveKitScreenPublisher& operator=(LiveKitScreenPublisher&&) = delete;

    [[nodiscard]] LiveKitPublishStartResult start(
        LiveKitPublishConfig configuration,
        std::uint32_t width,
        std::uint32_t height) noexcept;
    [[nodiscard]] bool submit(BgraVideoFrame frame) noexcept;
    void stop() noexcept;
    [[nodiscard]] LiveKitPublishSnapshot snapshot() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace screen_share::media
