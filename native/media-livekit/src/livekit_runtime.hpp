#pragma once

namespace screen_share::media::detail {

class LiveKitRuntimeLease final {
public:
    LiveKitRuntimeLease();
    ~LiveKitRuntimeLease();

    LiveKitRuntimeLease(const LiveKitRuntimeLease&) = delete;
    LiveKitRuntimeLease& operator=(const LiveKitRuntimeLease&) = delete;
};

}  // namespace screen_share::media::detail
