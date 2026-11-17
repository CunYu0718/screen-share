#include "validation_report.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>
#include <QSysInfo>

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace screen_share::desktop {
namespace {

constexpr std::int64_t expected_sample_interval_milliseconds = 500;

[[nodiscard]] std::uint64_t process_working_set_bytes() noexcept {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = static_cast<DWORD>(sizeof(counters));
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            static_cast<DWORD>(sizeof(counters))) == FALSE) {
        return 0;
    }
    return static_cast<std::uint64_t>(counters.WorkingSetSize);
}

[[nodiscard]] QString stage_name(const diagnostics::PipelineStage stage) {
    const std::string_view name = diagnostics::to_string(stage);
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

[[nodiscard]] QJsonObject stage_values(
    const std::array<double, diagnostics::pipeline_stage_count>& values) {
    QJsonObject result;
    for (std::size_t index = 0; index < diagnostics::pipeline_stage_count; ++index) {
        const auto stage = static_cast<diagnostics::PipelineStage>(index);
        result.insert(stage_name(stage), values[index]);
    }
    return result;
}

}  // namespace

QString validation_role_name(const ValidationRole role) {
    switch (role) {
        case ValidationRole::Host:
            return QStringLiteral("host");
        case ValidationRole::Viewer:
            return QStringLiteral("viewer");
    }
    return QStringLiteral("unknown");
}

void ValidationReportRecorder::start(const ValidationRole role) {
    role_ = role;
    active_ = true;
    started_steady_ = std::chrono::steady_clock::now();
    started_at_utc_ = QDateTime::currentDateTimeUtc();
    samples_.clear();
    samples_.reserve(static_cast<std::size_t>(target_duration_milliseconds /
                                               expected_sample_interval_milliseconds) +
                     16U);
}

ValidationRecordResult ValidationReportRecorder::record(const ValidationSampleInput& input) {
    if (!active_) {
        return {};
    }

    Sample sample;
    sample.elapsed_milliseconds = elapsed_milliseconds();
    sample.rates = input.rates.frames_per_second;
    for (std::size_t index = 0; index < diagnostics::pipeline_stage_count; ++index) {
        sample.durations_milliseconds[index] = input.pipeline.average_duration_milliseconds(
            static_cast<diagnostics::PipelineStage>(index));
    }
    sample.width = input.width;
    sample.height = input.height;
    sample.bitrate_bps = input.bitrate_bps;
    sample.packet_loss_ratio = input.packet_loss_ratio;
    sample.dropped_frames = input.dropped_frames;
    sample.working_set_bytes = process_working_set_bytes();
    sample.media_active = input.media_active;
    samples_.push_back(sample);

    if (sample.elapsed_milliseconds < target_duration_milliseconds) {
        return {};
    }
    return write_report(sample.elapsed_milliseconds);
}

ValidationRecordResult ValidationReportRecorder::stop_and_write() {
    if (!active_) {
        return {};
    }
    return write_report(elapsed_milliseconds());
}

bool ValidationReportRecorder::active() const noexcept {
    return active_;
}

ValidationRole ValidationReportRecorder::role() const noexcept {
    return role_;
}

std::int64_t ValidationReportRecorder::elapsed_milliseconds() const noexcept {
    if (!active_) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started_steady_)
        .count();
}

ValidationRecordResult ValidationReportRecorder::write_report(
    const std::int64_t elapsed_milliseconds) {
    const auto finished_at_utc = QDateTime::currentDateTimeUtc();
    active_ = false;

    std::array<double, diagnostics::pipeline_stage_count> average_rates{};
    std::array<double, diagnostics::pipeline_stage_count> maximum_rates{};
    double average_bitrate_bps = 0.0;
    double average_packet_loss_ratio = 0.0;
    std::uint64_t active_samples = 0;
    std::int64_t longest_render_gap_milliseconds = 0;
    std::int64_t render_gap_started_milliseconds = -1;

    for (const auto& sample : samples_) {
        for (std::size_t index = 0; index < diagnostics::pipeline_stage_count; ++index) {
            average_rates[index] += sample.rates[index];
            maximum_rates[index] = std::max(maximum_rates[index], sample.rates[index]);
        }
        average_bitrate_bps += static_cast<double>(sample.bitrate_bps);
        average_packet_loss_ratio += sample.packet_loss_ratio;
        if (sample.media_active) {
            ++active_samples;
        }

        const auto render_rate = sample.rates[static_cast<std::size_t>(
            diagnostics::PipelineStage::Render)];
        if (role_ == ValidationRole::Viewer && sample.media_active && render_rate < 1.0) {
            if (render_gap_started_milliseconds < 0) {
                render_gap_started_milliseconds = sample.elapsed_milliseconds;
            }
        } else if (render_gap_started_milliseconds >= 0) {
            longest_render_gap_milliseconds = std::max(
                longest_render_gap_milliseconds,
                sample.elapsed_milliseconds - render_gap_started_milliseconds);
            render_gap_started_milliseconds = -1;
        }
    }

    if (render_gap_started_milliseconds >= 0) {
        longest_render_gap_milliseconds = std::max(
            longest_render_gap_milliseconds,
            elapsed_milliseconds - render_gap_started_milliseconds);
    }

    const double sample_count = static_cast<double>(samples_.size());
    if (sample_count > 0.0) {
        for (double& rate : average_rates) {
            rate /= sample_count;
        }
        average_bitrate_bps /= sample_count;
        average_packet_loss_ratio /= sample_count;
    }

    const auto baseline_from = std::min<std::int64_t>(60'000, elapsed_milliseconds / 4);
    const auto baseline_to = std::min<std::int64_t>(120'000, elapsed_milliseconds / 2);
    const auto final_from = std::max<std::int64_t>(0, elapsed_milliseconds - 60'000);
    const auto average_working_set = [this](const std::int64_t from_milliseconds,
                                            const std::int64_t to_milliseconds) {
        double total = 0.0;
        std::uint64_t count = 0;
        for (const auto& sample : samples_) {
            if (sample.elapsed_milliseconds < from_milliseconds ||
                sample.elapsed_milliseconds > to_milliseconds ||
                sample.working_set_bytes == 0U) {
                continue;
            }
            total += static_cast<double>(sample.working_set_bytes);
            ++count;
        }
        return count == 0U ? 0.0 : total / static_cast<double>(count);
    };
    const double baseline_memory = average_working_set(baseline_from, baseline_to);
    const double final_memory = average_working_set(final_from, elapsed_milliseconds);
    const double memory_growth_ratio = baseline_memory > 0.0
                                           ? (final_memory - baseline_memory) / baseline_memory
                                           : 0.0;

    const auto capture_index =
        static_cast<std::size_t>(diagnostics::PipelineStage::Capture);
    const auto encode_index = static_cast<std::size_t>(diagnostics::PipelineStage::Encode);
    const auto send_index = static_cast<std::size_t>(diagnostics::PipelineStage::Send);
    const auto receive_index = static_cast<std::size_t>(diagnostics::PipelineStage::Receive);
    const auto decode_index = static_cast<std::size_t>(diagnostics::PipelineStage::Decode);
    const auto render_index = static_cast<std::size_t>(diagnostics::PipelineStage::Render);
    const bool endpoint_metrics_complete =
        role_ == ValidationRole::Host
            ? maximum_rates[capture_index] > 0.0 && maximum_rates[encode_index] > 0.0 &&
                  maximum_rates[send_index] > 0.0
            : maximum_rates[receive_index] > 0.0 && maximum_rates[decode_index] > 0.0 &&
                  maximum_rates[render_index] > 0.0;

    QJsonObject checks;
    checks.insert(QStringLiteral("duration_30_minutes"),
                  elapsed_milliseconds >= target_duration_milliseconds);
    checks.insert(QStringLiteral("endpoint_pipeline_metrics_complete"),
                  endpoint_metrics_complete);
    checks.insert(QStringLiteral("memory_growth_at_most_10_percent"),
                  baseline_memory > 0.0 && memory_growth_ratio <= 0.10);
    checks.insert(QStringLiteral("media_active_for_all_samples"),
                  !samples_.empty() && active_samples == samples_.size());
    if (role_ == ValidationRole::Viewer) {
        checks.insert(QStringLiteral("average_render_at_least_55_fps"),
                      average_rates[render_index] >= 55.0);
        checks.insert(QStringLiteral("no_observed_render_gap_over_500_ms"),
                      longest_render_gap_milliseconds <= 500);
    } else {
        checks.insert(QStringLiteral("average_render_at_least_55_fps"),
                      QJsonValue(QJsonValue::Null));
        checks.insert(QStringLiteral("no_observed_render_gap_over_500_ms"),
                      QJsonValue(QJsonValue::Null));
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("duration_seconds"),
                   static_cast<double>(elapsed_milliseconds) / 1000.0);
    summary.insert(QStringLiteral("sample_count"), static_cast<qint64>(samples_.size()));
    summary.insert(QStringLiteral("average_fps"), stage_values(average_rates));
    summary.insert(QStringLiteral("average_bitrate_bps"), average_bitrate_bps);
    summary.insert(QStringLiteral("average_packet_loss_ratio"),
                   average_packet_loss_ratio);
    summary.insert(QStringLiteral("memory_baseline_working_set_bytes"), baseline_memory);
    summary.insert(QStringLiteral("memory_final_working_set_bytes"), final_memory);
    summary.insert(QStringLiteral("memory_growth_ratio"), memory_growth_ratio);
    summary.insert(QStringLiteral("longest_observed_render_gap_ms"),
                   longest_render_gap_milliseconds);
    summary.insert(QStringLiteral("automated_checks"), checks);
    summary.insert(QStringLiteral("gate_status"), QStringLiteral("incomplete"));
    summary.insert(QStringLiteral("gate_status_reason"),
                   QStringLiteral("QG-1 also requires a paired endpoint report, measured P95 end-to-end latency, and manual visual/crash/device-loss review."));

    QJsonArray sample_array;
    for (const auto& sample : samples_) {
        QJsonObject item;
        item.insert(QStringLiteral("elapsed_ms"), sample.elapsed_milliseconds);
        item.insert(QStringLiteral("media_active"), sample.media_active);
        item.insert(QStringLiteral("width"), static_cast<qint64>(sample.width));
        item.insert(QStringLiteral("height"), static_cast<qint64>(sample.height));
        item.insert(QStringLiteral("fps"), stage_values(sample.rates));
        item.insert(QStringLiteral("average_stage_duration_ms"),
                    stage_values(sample.durations_milliseconds));
        item.insert(QStringLiteral("bitrate_bps"),
                    static_cast<qint64>(sample.bitrate_bps));
        item.insert(QStringLiteral("packet_loss_ratio"), sample.packet_loss_ratio);
        item.insert(QStringLiteral("dropped_frames"),
                    static_cast<qint64>(sample.dropped_frames));
        item.insert(QStringLiteral("working_set_bytes"),
                    static_cast<qint64>(sample.working_set_bytes));
        sample_array.append(item);
    }

    QJsonObject system;
    system.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    system.insert(QStringLiteral("kernel_type"), QSysInfo::kernelType());
    system.insert(QStringLiteral("kernel_version"), QSysInfo::kernelVersion());
    system.insert(QStringLiteral("current_cpu_architecture"),
                  QSysInfo::currentCpuArchitecture());
    system.insert(QStringLiteral("build_cpu_architecture"), QSysInfo::buildCpuArchitecture());
    system.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));

    QJsonObject root;
    root.insert(QStringLiteral("schema"),
                QStringLiteral("screen-share.qg1.endpoint-report.v1"));
    root.insert(QStringLiteral("contains_screen_content"), false);
    root.insert(QStringLiteral("contains_credentials"), false);
    root.insert(QStringLiteral("endpoint_role"), validation_role_name(role_));
    root.insert(QStringLiteral("app_version"), QCoreApplication::applicationVersion());
    root.insert(QStringLiteral("started_at_utc"), started_at_utc_.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("finished_at_utc"),
                finished_at_utc.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("target_duration_seconds"),
                target_duration_milliseconds / 1000);
    root.insert(QStringLiteral("system"), system);
    root.insert(QStringLiteral("summary"), summary);
    root.insert(QStringLiteral("samples"), sample_array);

    QString base_path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base_path.isEmpty()) {
        base_path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    QDir directory(base_path);
    const QString relative_directory = QStringLiteral("ScreenShare/validation");
    if (!directory.mkpath(relative_directory) || !directory.cd(relative_directory)) {
        return {.completed = true,
                .error_message = QStringLiteral("无法创建验收报告目录：%1")
                                     .arg(directory.filePath(relative_directory))};
    }

    const QString file_name = QStringLiteral("qg1-%1-%2.json")
                                  .arg(validation_role_name(role_),
                                       started_at_utc_.toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString report_path = directory.filePath(file_name);
    QFile report(report_path);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {.completed = true,
                .error_message = QStringLiteral("无法写入验收报告：%1")
                                     .arg(report.errorString())};
    }
    const auto payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (report.write(payload) != payload.size()) {
        return {.completed = true,
                .error_message = QStringLiteral("验收报告写入不完整：%1")
                                     .arg(report.errorString())};
    }
    report.close();
    return {.completed = true, .report_path = report_path};
}

}  // namespace screen_share::desktop
