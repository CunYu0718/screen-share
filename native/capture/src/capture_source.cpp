#include "screen_share/capture/capture_source.hpp"

#include <algorithm>

namespace screen_share::capture {

bool CaptureSourceSnapshot::complete() const noexcept {
    return status == CaptureSourceEnumerationStatus::Complete;
}

const CaptureSource* CaptureSourceSnapshot::find(const CaptureSourceId id) const noexcept {
    const auto source = std::find_if(sources.begin(), sources.end(), [id](const CaptureSource& candidate) {
        return candidate.id == id;
    });

    return source == sources.end() ? nullptr : &*source;
}

}  // namespace screen_share::capture
