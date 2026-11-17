#include "screen_share/media/livekit_screen_publisher.hpp"

#include "livekit_runtime.hpp"

#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace screen_share::media {
namespace {

[[nodiscard]] bool dimensions_supported(
    const std::uint32_t width, const std::uint32_t height) noexcept {
    const auto maximum_dimension = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    return width > 0U && height > 0U && width <= maximum_dimension &&
           height <= maximum_dimension;
}

}  // namespace

struct LiveKitScreenPublisher::Impl final {
    detail::LiveKitRuntimeLease runtime;
    mutable std::mutex lifecycle_mutex;
    std::thread worker;

    mutable std::mutex queue_mutex;
    std::condition_variable queue_changed;
    std::optional<BgraVideoFrame> pending_frame;
    bool stop_requested{false};

    std::atomic<LiveKitPublishState> state{LiveKitPublishState::Idle};
    std::atomic<LiveKitPublishError> error{LiveKitPublishError::None};
    std::atomic<std::uint64_t> accepted_frames{0};
    std::atomic<std::uint64_t> published_frames{0};
    std::atomic<std::uint64_t> dropped_frames{0};
    std::atomic<std::uint32_t> frame_width{0};
    std::atomic<std::uint32_t> frame_height{0};

    ~Impl() {
        stop();
    }

    void set_failure(const LiveKitPublishError next_error) noexcept {
        error.store(next_error, std::memory_order_release);
        state.store(LiveKitPublishState::Failed, std::memory_order_release);
    }

    [[nodiscard]] bool stopping() const noexcept {
        const std::scoped_lock lock(queue_mutex);
        return stop_requested;
    }

    void run(LiveKitPublishConfig configuration,
             const std::uint32_t width,
             const std::uint32_t height) noexcept {
        std::unique_ptr<livekit::Room> room;

        try {
            room = std::make_unique<livekit::Room>();
            livekit::RoomOptions room_options;
            room_options.auto_subscribe = false;
            room_options.dynacast = true;
            room_options.connect_timeout = std::chrono::seconds{5};

            if (!room->connect(
                    configuration.server_url, configuration.access_token, room_options)) {
                set_failure(LiveKitPublishError::ConnectionFailed);
                return;
            }
            if (stopping()) {
                static_cast<void>(room->disconnect());
                return;
            }

            auto participant = room->localParticipant().lock();
            if (!participant) {
                set_failure(LiveKitPublishError::ParticipantUnavailable);
                static_cast<void>(room->disconnect());
                return;
            }

            auto video_source = std::make_shared<livekit::VideoSource>(
                static_cast<int>(width), static_cast<int>(height));
            auto video_track = participant->publishVideoTrack(
                configuration.track_name,
                video_source,
                livekit::TrackSource::SOURCE_SCREENSHARE);
            if (!video_track) {
                set_failure(LiveKitPublishError::TrackPublicationFailed);
                static_cast<void>(room->disconnect());
                return;
            }

            participant.reset();
            state.store(LiveKitPublishState::Publishing, std::memory_order_release);

            while (true) {
                std::optional<BgraVideoFrame> next_frame;
                {
                    std::unique_lock lock(queue_mutex);
                    queue_changed.wait(lock, [this]() {
                        return stop_requested || pending_frame.has_value();
                    });
                    if (stop_requested) {
                        break;
                    }
                    next_frame = std::move(pending_frame);
                    pending_frame.reset();
                }

                if (!next_frame || !next_frame->valid()) {
                    dropped_frames.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                livekit::VideoFrame frame(
                    static_cast<int>(next_frame->width),
                    static_cast<int>(next_frame->height),
                    livekit::VideoBufferType::BGRA,
                    std::move(next_frame->pixels));
                livekit::VideoCaptureOptions capture_options;
                capture_options.timestamp_us = next_frame->timestamp_us;
                video_source->captureFrame(frame, capture_options);
                published_frames.fetch_add(1, std::memory_order_relaxed);
            }

            video_track.reset();
            video_source.reset();
            static_cast<void>(room->disconnect());
        } catch (...) {
            if (!stopping()) {
                set_failure(state.load(std::memory_order_acquire) ==
                                    LiveKitPublishState::Connecting
                                ? LiveKitPublishError::ConnectionFailed
                                : LiveKitPublishError::FramePublicationFailed);
            }
            if (room) {
                try {
                    static_cast<void>(room->disconnect());
                } catch (...) {
                }
            }
        }
    }

    [[nodiscard]] LiveKitPublishStartResult start(
        LiveKitPublishConfig configuration,
        const std::uint32_t width,
        const std::uint32_t height) noexcept {
        if (!is_valid_livekit_url(configuration.server_url) ||
            configuration.access_token.empty() || configuration.track_name.empty() ||
            !dimensions_supported(width, height)) {
            return LiveKitPublishStartResult::InvalidConfiguration;
        }

        const std::scoped_lock lifecycle_lock(lifecycle_mutex);
        const auto current_state = state.load(std::memory_order_acquire);
        if (current_state == LiveKitPublishState::Connecting ||
            current_state == LiveKitPublishState::Publishing) {
            return LiveKitPublishStartResult::AlreadyActive;
        }
        if (worker.joinable()) {
            worker.join();
        }

        {
            const std::scoped_lock queue_lock(queue_mutex);
            stop_requested = false;
            pending_frame.reset();
        }
        error.store(LiveKitPublishError::None, std::memory_order_relaxed);
        accepted_frames.store(0, std::memory_order_relaxed);
        published_frames.store(0, std::memory_order_relaxed);
        dropped_frames.store(0, std::memory_order_relaxed);
        frame_width.store(width, std::memory_order_relaxed);
        frame_height.store(height, std::memory_order_relaxed);
        state.store(LiveKitPublishState::Connecting, std::memory_order_release);

        try {
            worker = std::thread(
                [this, configuration = std::move(configuration), width, height]() mutable {
                    run(std::move(configuration), width, height);
                });
        } catch (...) {
            set_failure(LiveKitPublishError::ConnectionFailed);
            return LiveKitPublishStartResult::FailedToStart;
        }
        return LiveKitPublishStartResult::Starting;
    }

    [[nodiscard]] bool submit(BgraVideoFrame frame) noexcept {
        if (state.load(std::memory_order_acquire) != LiveKitPublishState::Publishing ||
            !frame.valid() || frame.width != frame_width.load(std::memory_order_relaxed) ||
            frame.height != frame_height.load(std::memory_order_relaxed)) {
            return false;
        }

        try {
            {
                const std::scoped_lock lock(queue_mutex);
                if (stop_requested ||
                    state.load(std::memory_order_relaxed) != LiveKitPublishState::Publishing) {
                    return false;
                }
                if (pending_frame.has_value()) {
                    dropped_frames.fetch_add(1, std::memory_order_relaxed);
                }
                pending_frame = std::move(frame);
                accepted_frames.fetch_add(1, std::memory_order_relaxed);
            }
            queue_changed.notify_one();
            return true;
        } catch (...) {
            return false;
        }
    }

    void stop() noexcept {
        std::thread worker_to_join;
        {
            const std::scoped_lock lifecycle_lock(lifecycle_mutex);
            {
                const std::scoped_lock queue_lock(queue_mutex);
                stop_requested = true;
                if (pending_frame.has_value()) {
                    pending_frame.reset();
                    dropped_frames.fetch_add(1, std::memory_order_relaxed);
                }
            }
            queue_changed.notify_all();
            if (worker.joinable()) {
                worker_to_join = std::move(worker);
            }
        }

        if (worker_to_join.joinable()) {
            worker_to_join.join();
        }
        state.store(LiveKitPublishState::Idle, std::memory_order_release);
        error.store(LiveKitPublishError::None, std::memory_order_release);
        frame_width.store(0, std::memory_order_relaxed);
        frame_height.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] LiveKitPublishSnapshot snapshot() const noexcept {
        return {
            .state = state.load(std::memory_order_acquire),
            .error = error.load(std::memory_order_acquire),
            .accepted_frames = accepted_frames.load(std::memory_order_relaxed),
            .published_frames = published_frames.load(std::memory_order_relaxed),
            .dropped_frames = dropped_frames.load(std::memory_order_relaxed),
            .width = frame_width.load(std::memory_order_relaxed),
            .height = frame_height.load(std::memory_order_relaxed),
        };
    }
};

LiveKitScreenPublisher::LiveKitScreenPublisher() : implementation_(std::make_unique<Impl>()) {
}

LiveKitScreenPublisher::~LiveKitScreenPublisher() {
    stop();
}

LiveKitPublishStartResult LiveKitScreenPublisher::start(
    LiveKitPublishConfig configuration,
    const std::uint32_t width,
    const std::uint32_t height) noexcept {
    return implementation_->start(std::move(configuration), width, height);
}

bool LiveKitScreenPublisher::submit(BgraVideoFrame frame) noexcept {
    return implementation_->submit(std::move(frame));
}

void LiveKitScreenPublisher::stop() noexcept {
    if (implementation_) {
        implementation_->stop();
    }
}

LiveKitPublishSnapshot LiveKitScreenPublisher::snapshot() const noexcept {
    return implementation_ ? implementation_->snapshot() : LiveKitPublishSnapshot{};
}

}  // namespace screen_share::media
