#include "livekit_runtime.hpp"

#include <livekit/livekit.h>

#include <cstdint>
#include <mutex>

namespace screen_share::media::detail {
namespace {

std::mutex runtime_mutex;
std::uint32_t runtime_users{0};

}  // namespace

LiveKitRuntimeLease::LiveKitRuntimeLease() {
    const std::scoped_lock lock(runtime_mutex);
    if (runtime_users == 0U) {
        static_cast<void>(livekit::initialize(livekit::LogLevel::Info));
    }
    ++runtime_users;
}

LiveKitRuntimeLease::~LiveKitRuntimeLease() {
    const std::scoped_lock lock(runtime_mutex);
    if (runtime_users > 0U) {
        --runtime_users;
        if (runtime_users == 0U) {
            livekit::shutdown();
        }
    }
}

}  // namespace screen_share::media::detail
