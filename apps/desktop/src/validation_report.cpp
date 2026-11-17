#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef SCREEN_SHARE_BUILD_COMMIT
#define SCREEN_SHARE_BUILD_COMMIT "unknown"
#endif

#ifndef SCREEN_SHARE_LIVEKIT_SDK_VERSION
#define SCREEN_SHARE_LIVEKIT_SDK_VERSION "unknown"
#endif

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

#include <d3d11.h>
#include <dxgi.h>
#include <Windows.h>
#include <Psapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace screen_share::desktop {
namespace {

constexpr std::int64_t expected_sample_interval_milliseconds = 500;

struct GraphicsAdapterInfo {
    QString description;
    QString driver_version;
    QString d3d_feature_level;
    QString error;
    std::uint32_t vendor_id{0};
    std::uint32_t device_id{0};
    std::uint64_t dedicated_video_memory_bytes{0};
};

[[nodiscard]] GraphicsAdapterInfo graphics_adapter_info() noexcept {
    GraphicsAdapterInfo result;
    try {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        D3D_FEATURE_LEVEL feature_level{};
        const HRESULT create_result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            device.GetAddressOf(),
            &feature_level,
            nullptr);
        if (FAILED(create_result) || !device) {
            result.error = QStringLiteral("D3D11CreateDevice failed: 0x%1")
                               .arg(static_cast<qulonglong>(
                                        static_cast<std::uint32_t>(create_result)),
                                    8,
                                    16,
                                    QChar('0'));
            return result;
        }

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if (FAILED(device.As(&dxgi_device)) ||
            FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf())) || !adapter) {
            result.error = QStringLiteral("Unable to resolve the DXGI adapter");
            return result;
        }

        DXGI_ADAPTER_DESC description{};
        if (FAILED(adapter->GetDesc(&description))) {
            result.error = QStringLiteral("IDXGIAdapter::GetDesc failed");
            return result;
        }

        result.description = QString::fromWCharArray(description.Description);
        result.vendor_id = description.VendorId;
        result.device_id = description.DeviceId;
        result.dedicated_video_memory_bytes =
            static_cast<std::uint64_t>(description.DedicatedVideoMemory);
        result.d3d_feature_level =
            QStringLiteral("0x%1").arg(static_cast<qulonglong>(feature_level), 4, 16, QChar('0'));

        LARGE_INTEGER driver_version{};
        if (SUCCEEDED(
                adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver_version))) {
            const auto high = static_cast<std::uint32_t>(driver_version.HighPart);
            const auto low = static_cast<std::uint32_t>(driver_version.LowPart);
            result.driver_version = QStringLiteral("%1.%2.%3.%4")
                                        .arg(static_cast<qulonglong>(HIWORD(high)))
                                        .arg(static_cast<qulonglong>(LOWORD(high)))
                                        .arg(static_cast<qulonglong>(HIWORD(low)))
                                        .arg(static_cast<qulonglong>(LOWORD(low)));
        } else {
            result.error = QStringLiteral("DXGI driver version unavailable");
        }
    } catch (...) {
        result.error = QStringLiteral("Unexpected graphics adapter query failure");
    }
    return result;
}

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

void ValidationReportRecorder::start(const ValidationRole role,
                                     ValidationEnvironment environment) {
    role_ = role;
    environment_ = std::move(environment);
    active_ = true;
    started_steady_ = std::chrono::steady_clock::now();
    started_at_utc_ = QDateTime::currentDateTimeUtc();
    samples_.clear();
    samples_.reserve(static_cast<std::size_t>(target_duration_milliseconds /
                                               expected_sample_interval_milliseconds) +
                     16U);
    latency_samples_milliseconds_.clear();
    latency_samples_milliseconds_.reserve(120'000U);
    latency_decode_failures_ = 0;
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

void ValidationReportRecorder::record_latency_sample(
    const std::uint64_t latency_milliseconds) noexcept {
    if (!active_ || role_ != ValidationRole::Viewer || latency_milliseconds > 10'000U) {
        return;
    }
    latency_samples_milliseconds_.push_back(latency_milliseconds);
}

void ValidationReportRecorder::record_latency_decode_failure() noexcept {
    if (active_ && role_ == ValidationRole::Viewer) {
        ++latency_decode_failures_;
    }
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
    const auto adapter = graphics_adapter_info();
    const bool hardware_metadata_complete = !adapter.description.isEmpty() &&
                                            !adapter.driver_version.isEmpty();
    const bool environment_metadata_complete = !environment_.sfu_version.trimmed().isEmpty() &&
                                               environment_.upstream_mbps > 0.0 &&
                                               environment_.downstream_mbps > 0.0 &&
                                               environment_.rtt_milliseconds >= 0.0 &&
                                               environment_.baseline_packet_loss_ratio >= 0.0;
    auto ordered_latency_samples = latency_samples_milliseconds_;
    std::sort(ordered_latency_samples.begin(), ordered_latency_samples.end());
    const auto latency_percentile = [&ordered_latency_samples](const std::size_t percentile) {
        if (ordered_latency_samples.empty()) {
            return std::uint64_t{0};
        }
        const auto rank =
            (ordered_latency_samples.size() * percentile + 99U) / 100U;
        return ordered_latency_samples[rank - 1U];
    };
    const auto latency_p50_milliseconds = latency_percentile(50U);
    const auto latency_p95_milliseconds = latency_percentile(95U);
    const auto latency_max_milliseconds = ordered_latency_samples.empty()
                                              ? std::uint64_t{0}
                                              : ordered_latency_samples.back();
    constexpr std::uint64_t minimum_latency_samples =
        static_cast<std::uint64_t>(target_duration_milliseconds / 1000) * 50U;
    const bool latency_samples_sufficient =
        latency_samples_milliseconds_.size() >= minimum_latency_samples;

    QJsonObject checks;
    checks.insert(QStringLiteral("duration_30_minutes"),
                  elapsed_milliseconds >= target_duration_milliseconds);
    checks.insert(QStringLiteral("endpoint_pipeline_metrics_complete"),
                  endpoint_metrics_complete);
    checks.insert(QStringLiteral("memory_growth_at_most_10_percent"),
                  baseline_memory > 0.0 && memory_growth_ratio <= 0.10);
    checks.insert(QStringLiteral("media_active_for_all_samples"),
                  !samples_.empty() && active_samples == samples_.size());
    checks.insert(QStringLiteral("hardware_metadata_complete"), hardware_metadata_complete);
    checks.insert(QStringLiteral("environment_metadata_complete"),
                  environment_metadata_complete);
    if (role_ == ValidationRole::Viewer) {
        checks.insert(QStringLiteral("average_render_at_least_55_fps"),
                      average_rates[render_index] >= 55.0);
        checks.insert(QStringLiteral("no_observed_render_gap_over_500_ms"),
                      longest_render_gap_milliseconds <= 500);
        checks.insert(QStringLiteral("visual_latency_samples_sufficient"),
                      latency_samples_sufficient);
        checks.insert(QStringLiteral("visual_latency_p95_at_most_500_ms"),
                      !ordered_latency_samples.empty() &&
                          latency_p95_milliseconds <= 500U);
    } else {
        checks.insert(QStringLiteral("average_render_at_least_55_fps"),
                      QJsonValue(QJsonValue::Null));
        checks.insert(QStringLiteral("no_observed_render_gap_over_500_ms"),
                      QJsonValue(QJsonValue::Null));
        checks.insert(QStringLiteral("visual_latency_samples_sufficient"),
                      QJsonValue(QJsonValue::Null));
        checks.insert(QStringLiteral("visual_latency_p95_at_most_500_ms"),
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
    summary.insert(QStringLiteral("visual_latency_sample_count"),
                   static_cast<qint64>(latency_samples_milliseconds_.size()));
    summary.insert(QStringLiteral("visual_latency_decode_failures"),
                   static_cast<qint64>(latency_decode_failures_));
    summary.insert(QStringLiteral("visual_latency_p50_ms"),
                   static_cast<qint64>(latency_p50_milliseconds));
    summary.insert(QStringLiteral("visual_latency_p95_ms"),
                   static_cast<qint64>(latency_p95_milliseconds));
    summary.insert(QStringLiteral("visual_latency_max_ms"),
                   static_cast<qint64>(latency_max_milliseconds));
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
    QJsonObject gpu;
    gpu.insert(QStringLiteral("description"), adapter.description);
    gpu.insert(QStringLiteral("driver_version"), adapter.driver_version);
    gpu.insert(QStringLiteral("d3d_feature_level"), adapter.d3d_feature_level);
    gpu.insert(QStringLiteral("vendor_id"), static_cast<qint64>(adapter.vendor_id));
    gpu.insert(QStringLiteral("device_id"), static_cast<qint64>(adapter.device_id));
    gpu.insert(QStringLiteral("dedicated_video_memory_bytes"),
               static_cast<qint64>(adapter.dedicated_video_memory_bytes));
    gpu.insert(QStringLiteral("query_error"), adapter.error);
    system.insert(QStringLiteral("gpu"), gpu);

    QJsonObject network_baseline;
    network_baseline.insert(QStringLiteral("upstream_mbps"), environment_.upstream_mbps);
    network_baseline.insert(QStringLiteral("downstream_mbps"),
                            environment_.downstream_mbps);
    network_baseline.insert(QStringLiteral("rtt_ms"), environment_.rtt_milliseconds);
    network_baseline.insert(QStringLiteral("packet_loss_ratio"),
                            environment_.baseline_packet_loss_ratio);

    QJsonObject environment;
    environment.insert(QStringLiteral("sfu_version"), environment_.sfu_version.trimmed());
    environment.insert(QStringLiteral("network_baseline"), network_baseline);

    QJsonObject root;
    root.insert(QStringLiteral("schema"),
                QStringLiteral("screen-share.qg1.endpoint-report.v2"));
    root.insert(QStringLiteral("contains_screen_content"), false);
    root.insert(QStringLiteral("contains_credentials"), false);
    root.insert(QStringLiteral("endpoint_role"), validation_role_name(role_));
    root.insert(QStringLiteral("app_version"), QCoreApplication::applicationVersion());
    root.insert(QStringLiteral("build_commit"),
                QString::fromLatin1(SCREEN_SHARE_BUILD_COMMIT));
    root.insert(QStringLiteral("livekit_sdk_version"),
                QString::fromLatin1(SCREEN_SHARE_LIVEKIT_SDK_VERSION));
    root.insert(QStringLiteral("started_at_utc"), started_at_utc_.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("finished_at_utc"),
                finished_at_utc.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("target_duration_seconds"),
                target_duration_milliseconds / 1000);
    root.insert(QStringLiteral("system"), system);
    root.insert(QStringLiteral("environment"), environment);
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
