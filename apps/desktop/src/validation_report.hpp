#pragma once

#include "screen_share/diagnostics/frame_pipeline_stats.hpp"

#include <QDateTime>
#include <QString>

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace screen_share::desktop {

enum class ValidationRole : std::uint8_t {
    Host,
    Viewer,
};

struct ValidationEnvironment {
    QString sfu_version;
    double upstream_mbps{0.0};
    double downstream_mbps{0.0};
    double rtt_milliseconds{0.0};
    double baseline_packet_loss_ratio{0.0};
};

struct ValidationSampleInput {
    diagnostics::FramePipelineRateSample rates{};
    diagnostics::FramePipelineSnapshot pipeline{};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint64_t bitrate_bps{0};
    double packet_loss_ratio{0.0};
    std::uint64_t dropped_frames{0};
    bool media_active{false};
};

struct ValidationRecordResult {
    bool completed{false};
    QString report_path;
    QString error_message;
};

class ValidationReportRecorder final {
public:
    static constexpr std::int64_t target_duration_milliseconds = 30 * 60 * 1000;

    void start(ValidationRole role, ValidationEnvironment environment);
    [[nodiscard]] ValidationRecordResult record(const ValidationSampleInput& input);
    void record_latency_sample(std::uint64_t latency_milliseconds) noexcept;
    void record_latency_decode_failure() noexcept;
    [[nodiscard]] ValidationRecordResult stop_and_write();

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] ValidationRole role() const noexcept;
    [[nodiscard]] std::int64_t elapsed_milliseconds() const noexcept;

private:
    struct Sample {
        std::int64_t elapsed_milliseconds{0};
        std::array<double, diagnostics::pipeline_stage_count> rates{};
        std::array<double, diagnostics::pipeline_stage_count> durations_milliseconds{};
        std::uint32_t width{0};
        std::uint32_t height{0};
        std::uint64_t bitrate_bps{0};
        double packet_loss_ratio{0.0};
        std::uint64_t dropped_frames{0};
        std::uint64_t working_set_bytes{0};
        bool media_active{false};
    };

    [[nodiscard]] ValidationRecordResult write_report(std::int64_t elapsed_milliseconds);

    ValidationRole role_{ValidationRole::Host};
    bool active_{false};
    std::chrono::steady_clock::time_point started_steady_{};
    QDateTime started_at_utc_;
    ValidationEnvironment environment_;
    std::vector<Sample> samples_;
    std::vector<std::uint64_t> latency_samples_milliseconds_;
    std::uint64_t latency_decode_failures_{0};
};

[[nodiscard]] QString validation_role_name(ValidationRole role);

}  // namespace screen_share::desktop
