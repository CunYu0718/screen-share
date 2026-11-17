#include "screen_share/capture/capture_source.hpp"
#include "screen_share/capture/wgc_capture_session.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string_view>
#include <utility>

namespace {

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    using screen_share::capture::CaptureSource;
    using screen_share::capture::CaptureSourceEnumerationStatus;
    using screen_share::capture::CaptureSourceId;
    using screen_share::capture::CaptureSourceKind;
    using screen_share::capture::CaptureSourceSnapshot;

    const CaptureSourceId monitor_id{CaptureSourceKind::Monitor, 101U};
    const CaptureSourceId window_id{CaptureSourceKind::Window, 202U};
    CaptureSourceSnapshot model{
        .sources = {
            CaptureSource{
                .id = monitor_id,
                .name = L"DISPLAY1",
                .bounds = {.x = 0, .y = 0, .width = 2560, .height = 1440},
                .primary = true,
            },
            CaptureSource{
                .id = window_id,
                .name = L"Test window",
                .bounds = {.x = 40, .y = 30, .width = 1280, .height = 720},
            },
        },
        .status = CaptureSourceEnumerationStatus::Complete,
    };

    check(model.complete(), "complete snapshot reports complete");
    check(model.find(monitor_id) != nullptr, "monitor can be selected by id");
    check(model.find(window_id) != nullptr, "window can be selected by id");
    check(model.find({CaptureSourceKind::Window, 303U}) == nullptr, "unknown source is rejected");

    const auto actual = screen_share::capture::CaptureSourceCatalog::enumerate();
    std::set<std::pair<CaptureSourceKind, std::uintptr_t>> unique_ids;
    for (const auto& source : actual.sources) {
        check(source.id.native_handle != 0U, "enumerated source has a native handle");
        check(!source.name.empty(), "enumerated source has a display name");
        check(unique_ids.emplace(source.id.kind, source.id.native_handle).second,
              "enumerated source ids are unique");
    }

    screen_share::capture::WgcCaptureSession capture_session;
    const auto invalid_start = capture_session.start(
        {CaptureSourceKind::Monitor, 0U}, [](const auto&) {});
    check(invalid_start == screen_share::capture::WgcCaptureStartResult::InvalidSource,
          "null native capture source is rejected without creating a GPU device");
    check(!capture_session.running(), "invalid capture session remains stopped");
    capture_session.stop();

    std::cout << "All capture source tests passed\n";
    return EXIT_SUCCESS;
}
