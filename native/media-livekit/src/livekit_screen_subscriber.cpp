#include "screen_share/media/livekit_screen_subscriber.hpp"

#include "livekit_runtime.hpp"

#include <livekit/livekit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace screen_share::media {
namespace {

[[nodiscard]] bool valid_frame_dimensions(const int width, const int height) noexcept {
    return width > 0 && height > 0;
}

[[nodiscard]] std::optional<std::size_t> bgra_buffer_size(
    const int width, const int height) noexcept {
    if (!valid_frame_dimensions(width, height)) {
        return std::nullopt;
    }

    constexpr auto bytes_per_pixel = std::size_t{4};
    const auto unsigned_width = static_cast<std::size_t>(width);
    const auto unsigned_height = static_cast<std::size_t>(height);
    if (unsigned_width > std::numeric_limits<std::size_t>::max() / bytes_per_pixel) {
        return std::nullopt;
    }
    const auto row_bytes = unsigned_width * bytes_per_pixel;
    if (unsigned_height > std::numeric_limits<std::size_t>::max() / row_bytes) {
        return std::nullopt;
    }
    return row_bytes * unsigned_height;
}

}  // namespace

struct LiveKitScreenSubscriber::Impl final {
    detail::LiveKitRuntimeLease runtime;
    mutable std::mutex lifecycle_mutex;
    std::thread worker;

    mutable std::mutex control_mutex;
    std::condition_variable control_changed;
    bool stop_requested{false};

    mutable std::mutex frame_mutex;
    std::optional<BgraVideoFrame> latest_frame;

    std::atomic<LiveKitSubscribeState> state{LiveKitSubscribeState::Idle};
    std::atomic<LiveKitSubscribeError> error{LiveKitSubscribeError::None};
    std::atomic<std::uint64_t> decoded_frames{0};
    std::atomic<std::uint64_t> delivered_frames{0};
    std::atomic<std::uint64_t> dropped_frames{0};
    std::atomic<std::uint64_t> conversion_failures{0};
    std::atomic<std::uint32_t> frame_width{0};
    std::atomic<std::uint32_t> frame_height{0};

    ~Impl() {
        stop();
    }

    [[nodiscard]] bool stopping() const noexcept {
        const std::scoped_lock lock(control_mutex);
        return stop_requested;
    }

    void set_failure(const LiveKitSubscribeError next_error) noexcept {
        error.store(next_error, std::memory_order_release);
        state.store(LiveKitSubscribeState::Failed, std::memory_order_release);
    }

    void on_frame(const livekit::VideoFrame& incoming, const std::int64_t timestamp_us) noexcept {
        decoded_frames.fetch_add(1, std::memory_order_relaxed);

        try {
            std::optional<livekit::VideoFrame> converted;
            const livekit::VideoFrame* bgra_frame = &incoming;
            if (incoming.type() != livekit::VideoBufferType::BGRA) {
                converted.emplace(incoming.convert(livekit::VideoBufferType::BGRA));
                bgra_frame = &converted.value();
            }

            const auto expected_size =
                bgra_buffer_size(bgra_frame->width(), bgra_frame->height());
            if (!expected_size || bgra_frame->dataSize() != expected_size.value()) {
                conversion_failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            BgraVideoFrame frame{
                .width = static_cast<std::uint32_t>(bgra_frame->width()),
                .height = static_cast<std::uint32_t>(bgra_frame->height()),
                .timestamp_us = timestamp_us,
                .pixels = std::vector<std::uint8_t>(
                    bgra_frame->data(), bgra_frame->data() + bgra_frame->dataSize()),
            };

            {
                const std::scoped_lock lock(frame_mutex);
                if (latest_frame.has_value()) {
                    dropped_frames.fetch_add(1, std::memory_order_relaxed);
                }
                latest_frame = std::move(frame);
            }
            frame_width.store(
                static_cast<std::uint32_t>(bgra_frame->width()), std::memory_order_relaxed);
            frame_height.store(
                static_cast<std::uint32_t>(bgra_frame->height()), std::memory_order_relaxed);
        } catch (...) {
            conversion_failures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void run(LiveKitSubscribeConfig configuration) noexcept {
        std::unique_ptr<livekit::Room> room;
        bool callback_registered = false;

        try {
            room = std::make_unique<livekit::Room>();
            livekit::VideoStream::Options stream_options;
            stream_options.capacity = 1;
            stream_options.format = livekit::VideoBufferType::BGRA;
            room->setOnVideoFrameCallback(
                configuration.publisher_identity,
                configuration.track_name,
                [this](const livekit::VideoFrame& frame, const std::int64_t timestamp_us) {
                    on_frame(frame, timestamp_us);
                },
                stream_options);
            callback_registered = true;

            livekit::RoomOptions room_options;
            room_options.auto_subscribe = true;
            room_options.adaptive_stream = false;
            room_options.connect_timeout = std::chrono::seconds{5};
            if (!room->connect(
                    configuration.server_url, configuration.access_token, room_options)) {
                set_failure(LiveKitSubscribeError::ConnectionFailed);
                room->clearOnVideoFrameCallback(
                    configuration.publisher_identity, configuration.track_name);
                return;
            }

            state.store(LiveKitSubscribeState::Watching, std::memory_order_release);
            {
                std::unique_lock lock(control_mutex);
                control_changed.wait(lock, [this]() { return stop_requested; });
            }

            room->clearOnVideoFrameCallback(
                configuration.publisher_identity, configuration.track_name);
            callback_registered = false;
            static_cast<void>(room->disconnect());
        } catch (...) {
            if (!stopping()) {
                set_failure(callback_registered
                                ? LiveKitSubscribeError::ConnectionFailed
                                : LiveKitSubscribeError::CallbackRegistrationFailed);
            }
            if (room) {
                if (callback_registered) {
                    try {
                        room->clearOnVideoFrameCallback(
                            configuration.publisher_identity, configuration.track_name);
                    } catch (...) {
                    }
                }
                try {
                    static_cast<void>(room->disconnect());
                } catch (...) {
                }
            }
        }
    }

    [[nodiscard]] LiveKitSubscribeStartResult start(
        LiveKitSubscribeConfig configuration) noexcept {
        if (!is_valid_livekit_url(configuration.server_url) ||
            configuration.access_token.empty() || configuration.publisher_identity.empty() ||
            configuration.track_name.empty()) {
            return LiveKitSubscribeStartResult::InvalidConfiguration;
        }

        const std::scoped_lock lifecycle_lock(lifecycle_mutex);
        const auto current_state = state.load(std::memory_order_acquire);
        if (current_state == LiveKitSubscribeState::Connecting ||
            current_state == LiveKitSubscribeState::Watching) {
            return LiveKitSubscribeStartResult::AlreadyActive;
        }
        if (worker.joinable()) {
            worker.join();
        }

        {
            const std::scoped_lock control_lock(control_mutex);
            stop_requested = false;
        }
        {
            const std::scoped_lock frame_lock(frame_mutex);
            latest_frame.reset();
        }
        error.store(LiveKitSubscribeError::None, std::memory_order_relaxed);
        decoded_frames.store(0, std::memory_order_relaxed);
        delivered_frames.store(0, std::memory_order_relaxed);
        dropped_frames.store(0, std::memory_order_relaxed);
        conversion_failures.store(0, std::memory_order_relaxed);
        frame_width.store(0, std::memory_order_relaxed);
        frame_height.store(0, std::memory_order_relaxed);
        state.store(LiveKitSubscribeState::Connecting, std::memory_order_release);

        try {
            worker = std::thread(
                [this, configuration = std::move(configuration)]() mutable {
                    run(std::move(configuration));
                });
        } catch (...) {
            set_failure(LiveKitSubscribeError::ConnectionFailed);
            return LiveKitSubscribeStartResult::FailedToStart;
        }
        return LiveKitSubscribeStartResult::Starting;
    }

    [[nodiscard]] bool take_latest(BgraVideoFrame& frame) noexcept {
        try {
            {
                const std::scoped_lock lock(frame_mutex);
                if (!latest_frame.has_value()) {
                    return false;
                }
                frame = std::move(latest_frame.value());
                latest_frame.reset();
            }
            delivered_frames.fetch_add(1, std::memory_order_relaxed);
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
                const std::scoped_lock control_lock(control_mutex);
                stop_requested = true;
            }
            control_changed.notify_all();
            if (worker.joinable()) {
                worker_to_join = std::move(worker);
            }
        }

        if (worker_to_join.joinable()) {
            worker_to_join.join();
        }
        {
            const std::scoped_lock frame_lock(frame_mutex);
            latest_frame.reset();
        }
        state.store(LiveKitSubscribeState::Idle, std::memory_order_release);
        error.store(LiveKitSubscribeError::None, std::memory_order_release);
        frame_width.store(0, std::memory_order_relaxed);
        frame_height.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] LiveKitSubscribeSnapshot snapshot() const noexcept {
        return {
            .state = state.load(std::memory_order_acquire),
            .error = error.load(std::memory_order_acquire),
            .decoded_frames = decoded_frames.load(std::memory_order_relaxed),
            .delivered_frames = delivered_frames.load(std::memory_order_relaxed),
            .dropped_frames = dropped_frames.load(std::memory_order_relaxed),
            .conversion_failures = conversion_failures.load(std::memory_order_relaxed),
            .width = frame_width.load(std::memory_order_relaxed),
            .height = frame_height.load(std::memory_order_relaxed),
        };
    }
};

LiveKitScreenSubscriber::LiveKitScreenSubscriber()
    : implementation_(std::make_unique<Impl>()) {
}

LiveKitScreenSubscriber::~LiveKitScreenSubscriber() {
    stop();
}

LiveKitSubscribeStartResult LiveKitScreenSubscriber::start(
    LiveKitSubscribeConfig configuration) noexcept {
    return implementation_->start(std::move(configuration));
}

bool LiveKitScreenSubscriber::take_latest(BgraVideoFrame& frame) noexcept {
    return implementation_->take_latest(frame);
}

void LiveKitScreenSubscriber::stop() noexcept {
    if (implementation_) {
        implementation_->stop();
    }
}

LiveKitSubscribeSnapshot LiveKitScreenSubscriber::snapshot() const noexcept {
    return implementation_ ? implementation_->snapshot() : LiveKitSubscribeSnapshot{};
}

}  // namespace screen_share::media
