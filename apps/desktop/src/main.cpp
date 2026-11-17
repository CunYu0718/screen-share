#include "screen_share/capture/capture_source.hpp"
#include "screen_share/capture/wgc_capture_session.hpp"
#include "screen_share/core/session_policy.hpp"
#include "screen_share/diagnostics/frame_pipeline_stats.hpp"
#include "screen_share/diagnostics/frame_rate_estimator.hpp"
#include "screen_share/media/d3d11_bgra_frame_reader.hpp"
#include "screen_share/media/livekit_screen_publisher.hpp"
#include "screen_share/media/livekit_screen_subscriber.hpp"
#include "screen_share/renderer/d3d11_preview_renderer.hpp"
#include "validation_report.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPaintEngine>
#include <QPushButton>
#include <QSizePolicy>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <winrt/base.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace {

class WindowsRuntimeApartment final {
public:
    WindowsRuntimeApartment() {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }

    ~WindowsRuntimeApartment() {
        winrt::uninit_apartment();
    }

    WindowsRuntimeApartment(const WindowsRuntimeApartment&) = delete;
    WindowsRuntimeApartment& operator=(const WindowsRuntimeApartment&) = delete;
};

struct LatestFrameState {
    std::atomic<std::uint32_t> width{0};
    std::atomic<std::uint32_t> height{0};
};

[[nodiscard]] std::int64_t steady_timestamp_100ns() noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / 100;
}

class D3DPreviewWidget final : public QWidget {
public:
    explicit D3DPreviewWidget(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMinimumSize(384, 216);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    [[nodiscard]] QPaintEngine* paintEngine() const override {
        return nullptr;
    }
};

[[nodiscard]] QString source_label(const screen_share::capture::CaptureSource& source) {
    const auto name = QString::fromWCharArray(
        source.name.data(), static_cast<qsizetype>(source.name.size()));

    if (source.id.kind == screen_share::capture::CaptureSourceKind::Monitor) {
        const auto primary = source.primary ? QStringLiteral(" · 主屏") : QString{};
        return QStringLiteral("屏幕 · %1 · %2×%3%4")
            .arg(name)
            .arg(source.bounds.width)
            .arg(source.bounds.height)
            .arg(primary);
    }

    const auto minimized = source.minimized ? QStringLiteral(" · 已最小化") : QString{};
    return QStringLiteral("窗口 · %1%2").arg(name, minimized);
}

[[nodiscard]] QString enumeration_message(
    const screen_share::capture::CaptureSourceSnapshot& snapshot) {
    using screen_share::capture::CaptureSourceEnumerationStatus;

    if (snapshot.sources.empty()) {
        return QStringLiteral("未找到可用的屏幕或窗口。");
    }

    switch (snapshot.status) {
        case CaptureSourceEnumerationStatus::Complete:
            return QStringLiteral("已找到 %1 个可分享源。")
                .arg(static_cast<qulonglong>(snapshot.sources.size()));
        case CaptureSourceEnumerationStatus::MonitorEnumerationFailed:
            return QStringLiteral("已加载窗口，但暂时无法枚举屏幕。");
        case CaptureSourceEnumerationStatus::WindowEnumerationFailed:
            return QStringLiteral("已加载屏幕，但暂时无法枚举窗口。");
        case CaptureSourceEnumerationStatus::Failed:
            return QStringLiteral("无法刷新屏幕和窗口列表。");
    }

    return QStringLiteral("无法刷新分享源列表。");
}

[[nodiscard]] QString capture_start_message(
    const screen_share::capture::WgcCaptureStartResult result) {
    using screen_share::capture::WgcCaptureStartResult;

    switch (result) {
        case WgcCaptureStartResult::Started:
            return QStringLiteral("捕获已启动，正在等待首帧…");
        case WgcCaptureStartResult::AlreadyRunning:
            return QStringLiteral("当前捕获会话已在运行。");
        case WgcCaptureStartResult::Unsupported:
            return QStringLiteral("当前 Windows 版本或会话不支持图形捕获。");
        case WgcCaptureStartResult::InvalidSource:
            return QStringLiteral("捕获源已失效，请刷新后重新选择。");
        case WgcCaptureStartResult::DeviceCreationFailed:
            return QStringLiteral("无法创建 D3D11 硬件设备。");
        case WgcCaptureStartResult::CaptureItemCreationFailed:
            return QStringLiteral("无法打开所选屏幕或窗口。");
        case WgcCaptureStartResult::SessionCreationFailed:
            return QStringLiteral("无法创建 Windows Graphics Capture 帧池。");
    }

    return QStringLiteral("无法启动屏幕捕获。");
}

[[nodiscard]] QString publish_start_message(
    const screen_share::media::LiveKitPublishStartResult result) {
    using screen_share::media::LiveKitPublishStartResult;

    switch (result) {
        case LiveKitPublishStartResult::Starting:
            return QStringLiteral("正在连接 LiveKit 并发布屏幕轨道…");
        case LiveKitPublishStartResult::AlreadyActive:
            return QStringLiteral("LiveKit 发布已在连接或运行。");
        case LiveKitPublishStartResult::InvalidConfiguration:
            return QStringLiteral("请检查 LiveKit URL、令牌和当前捕获尺寸。");
        case LiveKitPublishStartResult::FailedToStart:
            return QStringLiteral("无法启动 LiveKit 发布工作线程。");
    }

    return QStringLiteral("无法启动 LiveKit 发布。");
}

[[nodiscard]] QString publish_error_message(
    const screen_share::media::LiveKitPublishError error) {
    using screen_share::media::LiveKitPublishError;

    switch (error) {
        case LiveKitPublishError::None:
            return QStringLiteral("无错误");
        case LiveKitPublishError::ConnectionFailed:
            return QStringLiteral("LiveKit 连接失败，请检查服务、URL 和令牌");
        case LiveKitPublishError::ParticipantUnavailable:
            return QStringLiteral("房间已连接，但无法取得本地参与者");
        case LiveKitPublishError::TrackPublicationFailed:
            return QStringLiteral("房间已连接，但屏幕轨道发布失败");
        case LiveKitPublishError::FramePublicationFailed:
            return QStringLiteral("向 LiveKit 提交视频帧时失败");
    }

    return QStringLiteral("未知发布错误");
}

[[nodiscard]] QString subscribe_start_message(
    const screen_share::media::LiveKitSubscribeStartResult result) {
    using screen_share::media::LiveKitSubscribeStartResult;

    switch (result) {
        case LiveKitSubscribeStartResult::Starting:
            return QStringLiteral("正在加入 LiveKit 房间并等待屏幕轨道…");
        case LiveKitSubscribeStartResult::AlreadyActive:
            return QStringLiteral("观看连接已在建立或运行。");
        case LiveKitSubscribeStartResult::InvalidConfiguration:
            return QStringLiteral("请检查 LiveKit URL、观看者令牌和发起者身份。");
        case LiveKitSubscribeStartResult::FailedToStart:
            return QStringLiteral("无法启动 LiveKit 观看工作线程。");
    }

    return QStringLiteral("无法启动 LiveKit 观看连接。");
}

[[nodiscard]] QString subscribe_error_message(
    const screen_share::media::LiveKitSubscribeError error) {
    using screen_share::media::LiveKitSubscribeError;

    switch (error) {
        case LiveKitSubscribeError::None:
            return QStringLiteral("无错误");
        case LiveKitSubscribeError::ConnectionFailed:
            return QStringLiteral("LiveKit 观看连接失败，请检查服务、URL 和令牌");
        case LiveKitSubscribeError::CallbackRegistrationFailed:
            return QStringLiteral("无法注册远端屏幕轨道回调");
    }

    return QStringLiteral("未知观看错误");
}

}  // namespace

int main(int argc, char* argv[]) {
    const WindowsRuntimeApartment runtime_apartment;
    QApplication application(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("Screen Share"));
    QApplication::setOrganizationName(QStringLiteral("Screen Share Contributors"));
    QApplication::setApplicationVersion(QString::fromLatin1(SCREEN_SHARE_PROJECT_VERSION));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Screen Share · M1"));
    window.resize(820, 1040);

    auto* central_widget = new QWidget(&window);
    auto* layout = new QVBoxLayout(central_widget);
    layout->setContentsMargins(48, 48, 48, 48);
    layout->setSpacing(16);

    auto* title = new QLabel(QStringLiteral("实时屏幕共享"), central_widget);
    auto title_font = title->font();
    title_font.setPointSize(22);
    title_font.setBold(true);
    title->setFont(title_font);

    auto* summary = new QLabel(
        QStringLiteral("统一 Windows 客户端 · 最多 %1 名观看者 · 目标 2K60")
            .arg(screen_share::core::maximum_viewer_count),
        central_widget);
    summary->setWordWrap(true);

    auto* source_group = new QGroupBox(QStringLiteral("选择分享源"), central_widget);
    auto* source_layout = new QVBoxLayout(source_group);
    auto* source_combo = new QComboBox(source_group);
    source_combo->setMinimumContentsLength(36);
    source_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto* source_message = new QLabel(QStringLiteral("正在枚举屏幕和窗口…"), source_group);
    source_message->setWordWrap(true);
    auto* refresh_button = new QPushButton(QStringLiteral("刷新列表"), source_group);
    source_layout->addWidget(source_combo);
    source_layout->addWidget(source_message);
    source_layout->addWidget(refresh_button);

    auto* preview_group = new QGroupBox(QStringLiteral("本地 GPU 预览"), central_widget);
    auto* preview_layout = new QVBoxLayout(preview_group);
    auto* preview_widget = new D3DPreviewWidget(preview_group);
    preview_layout->addWidget(preview_widget);

    auto* publish_group = new QGroupBox(QStringLiteral("LiveKit 房间"), central_widget);
    auto* publish_layout = new QFormLayout(publish_group);
    auto* server_url = new QLineEdit(QStringLiteral("ws://localhost:7880"), publish_group);
    auto* access_token = new QLineEdit(publish_group);
    access_token->setEchoMode(QLineEdit::Password);
    access_token->setPlaceholderText(QStringLiteral("粘贴当前角色的短时令牌"));
    auto* publisher_identity = new QLineEdit(publish_group);
    publisher_identity->setPlaceholderText(QStringLiteral("观看端填写发起者 identity"));
    auto* publish_button = new QPushButton(QStringLiteral("开始发布"), publish_group);
    publish_layout->addRow(QStringLiteral("服务器 URL"), server_url);
    publish_layout->addRow(QStringLiteral("访问令牌"), access_token);
    publish_layout->addRow(QStringLiteral("发起者身份"), publisher_identity);
    publish_layout->addRow(publish_button);

    auto* validation_environment_group =
        new QGroupBox(QStringLiteral("实机验收环境"), central_widget);
    auto* validation_environment_layout = new QFormLayout(validation_environment_group);
    auto* sfu_version =
        new QLineEdit(QStringLiteral("1.13.11"), validation_environment_group);
    auto* upstream_mbps = new QLineEdit(validation_environment_group);
    auto* downstream_mbps = new QLineEdit(validation_environment_group);
    auto* rtt_milliseconds = new QLineEdit(validation_environment_group);
    auto* baseline_packet_loss_percent = new QLineEdit(validation_environment_group);
    upstream_mbps->setPlaceholderText(QStringLiteral("例如 100"));
    downstream_mbps->setPlaceholderText(QStringLiteral("例如 100"));
    rtt_milliseconds->setPlaceholderText(QStringLiteral("例如 12.5"));
    baseline_packet_loss_percent->setPlaceholderText(QStringLiteral("例如 0 或 0.2"));
    for (auto* input :
         {upstream_mbps, downstream_mbps, rtt_milliseconds, baseline_packet_loss_percent}) {
        auto* validator = new QDoubleValidator(0.0, 100'000.0, 3, input);
        validator->setNotation(QDoubleValidator::StandardNotation);
        input->setValidator(validator);
    }
    auto* network_baseline_confirmed = new QCheckBox(
        QStringLiteral("以上网络数据已在本次运行前实测"), validation_environment_group);
    validation_environment_layout->addRow(QStringLiteral("LiveKit SFU 版本"), sfu_version);
    validation_environment_layout->addRow(QStringLiteral("上行 Mbps"), upstream_mbps);
    validation_environment_layout->addRow(QStringLiteral("下行 Mbps"), downstream_mbps);
    validation_environment_layout->addRow(QStringLiteral("RTT ms"), rtt_milliseconds);
    validation_environment_layout->addRow(QStringLiteral("基线丢包 %"),
                                          baseline_packet_loss_percent);
    validation_environment_layout->addRow(network_baseline_confirmed);

    auto* status = new QLabel(QStringLiteral("M1-07：请选择屏幕或窗口"), central_widget);
    status->setWordWrap(true);
    auto* host_button = new QPushButton(QStringLiteral("开始捕获"), central_widget);
    auto* join_button = new QPushButton(QStringLiteral("加入观看"), central_widget);
    auto* validation_button =
        new QPushButton(QStringLiteral("开始 30 分钟验收记录"), central_widget);
    auto* validation_status = new QLabel(
        QStringLiteral("M1-07：尚未记录；报告不包含令牌、窗口标题或屏幕内容。"),
        central_widget);
    validation_status->setWordWrap(true);
    const auto capture_session = std::make_shared<screen_share::capture::WgcCaptureSession>();
    const auto latest_frame = std::make_shared<LatestFrameState>();
    const auto frame_rate = std::make_shared<screen_share::diagnostics::FrameRateEstimator>();
    const auto pipeline_stats =
        std::make_shared<screen_share::diagnostics::FramePipelineStats>();
    const auto pipeline_rate =
        std::make_shared<screen_share::diagnostics::FramePipelineRateEstimator>();
    const auto preview_handle = reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<void*>(preview_widget->winId()));
    const auto preview_renderer =
        std::make_shared<screen_share::renderer::D3D11PreviewRenderer>(preview_handle);
    const auto frame_reader =
        std::make_shared<screen_share::media::D3D11BgraFrameReader>();
    const auto screen_publisher =
        std::make_shared<screen_share::media::LiveKitScreenPublisher>();
    const auto screen_subscriber =
        std::make_shared<screen_share::media::LiveKitScreenSubscriber>();
    const auto readback_failures = std::make_shared<std::atomic<std::uint64_t>>(0);
    const auto capture_active_in_ui = std::make_shared<bool>(false);
    const auto validation_recorder =
        std::make_shared<screen_share::desktop::ValidationReportRecorder>();

    const auto show_validation_result =
        [validation_recorder, validation_button, validation_status](
            const screen_share::desktop::ValidationRecordResult& result) {
            if (result.completed) {
                validation_button->setText(QStringLiteral("开始 30 分钟验收记录"));
                if (!result.error_message.isEmpty()) {
                    validation_status->setText(result.error_message);
                } else {
                    validation_status->setText(
                        QStringLiteral("验收端点报告已保存：%1").arg(result.report_path));
                }
                return;
            }
            if (!validation_recorder->active()) {
                return;
            }
            const auto elapsed_seconds = validation_recorder->elapsed_milliseconds() / 1000;
            validation_status->setText(
                QStringLiteral("正在记录 %1 端 · %2:%3 / 30:00")
                    .arg(screen_share::desktop::validation_role_name(
                        validation_recorder->role()))
                    .arg(elapsed_seconds / 60, 2, 10, QChar('0'))
                    .arg(elapsed_seconds % 60, 2, 10, QChar('0')));
        };

    const auto record_validation =
        [validation_recorder, show_validation_result](
            const screen_share::desktop::ValidationRole role,
            const screen_share::desktop::ValidationSampleInput& input) {
            if (!validation_recorder->active() || validation_recorder->role() != role) {
                return;
            }
            show_validation_result(validation_recorder->record(input));
        };

    const auto refresh_sources = [source_combo, source_message]() {
        source_combo->clear();
        const auto snapshot = screen_share::capture::CaptureSourceCatalog::enumerate();

        for (const auto& source : snapshot.sources) {
            source_combo->addItem(source_label(source));
            const auto index = source_combo->count() - 1;
            source_combo->setItemData(
                index,
                QVariant::fromValue(static_cast<qulonglong>(source.id.native_handle)),
                Qt::UserRole);
            source_combo->setItemData(
                index, static_cast<int>(source.id.kind), Qt::UserRole + 1);
        }

        source_message->setText(enumeration_message(snapshot));
        source_combo->setEnabled(!snapshot.sources.empty());
    };

    QObject::connect(refresh_button, &QPushButton::clicked, source_group, refresh_sources);
    QObject::connect(source_combo, &QComboBox::currentIndexChanged, status,
                     [source_combo, status](const int index) {
                         if (index >= 0) {
                             status->setText(
                                 QStringLiteral("当前选择：%1").arg(source_combo->itemText(index)));
                         }
                     });
    QObject::connect(
        host_button,
        &QPushButton::clicked,
        status,
        [capture_session,
         latest_frame,
         frame_rate,
         pipeline_stats,
         pipeline_rate,
         preview_renderer,
         frame_reader,
         screen_publisher,
         screen_subscriber,
         readback_failures,
         capture_active_in_ui,
         source_combo,
         refresh_button,
         host_button,
         publish_button,
         join_button,
         status]() {
            if (*capture_active_in_ui) {
                capture_session->stop();
                screen_publisher->stop();
                frame_reader->reset();
                preview_renderer->reset();
                *capture_active_in_ui = false;
                source_combo->setEnabled(source_combo->count() > 0);
                refresh_button->setEnabled(true);
                host_button->setText(QStringLiteral("开始捕获"));
                publish_button->setText(QStringLiteral("开始发布"));
                status->setText(QStringLiteral("捕获已停止。"));
                return;
            }

            if (source_combo->currentIndex() < 0) {
                status->setText(QStringLiteral("请先刷新并选择一个可分享源。"));
                return;
            }

            const auto source = screen_share::capture::CaptureSourceId{
                .kind = static_cast<screen_share::capture::CaptureSourceKind>(
                    source_combo->currentData(Qt::UserRole + 1).toInt()),
                .native_handle = static_cast<std::uintptr_t>(
                    source_combo->currentData(Qt::UserRole).toULongLong()),
            };

            latest_frame->width.store(0, std::memory_order_relaxed);
            latest_frame->height.store(0, std::memory_order_relaxed);
            frame_rate->reset();
            pipeline_stats->reset();
            pipeline_rate->reset();
            screen_subscriber->stop();
            preview_renderer->reset();
            screen_publisher->stop();
            frame_reader->reset();
            readback_failures->store(0, std::memory_order_relaxed);
            join_button->setText(QStringLiteral("加入观看"));

            const auto result = capture_session->start(
                source,
                [latest_frame,
                 frame_rate,
                 pipeline_stats,
                 preview_renderer,
                 frame_reader,
                 screen_publisher,
                 readback_failures](
                    const screen_share::capture::D3D11CaptureFrameView& frame) {
                    const auto capture_started = std::chrono::steady_clock::now();
                    latest_frame->width.store(frame.width, std::memory_order_relaxed);
                    latest_frame->height.store(frame.height, std::memory_order_relaxed);
                    frame_rate->record(frame.system_relative_time_100ns);
                    pipeline_stats->record(screen_share::diagnostics::PipelineStage::Capture);
                    const auto render_started = std::chrono::steady_clock::now();
                    if (preview_renderer->render(frame) ==
                        screen_share::renderer::PreviewRenderResult::Rendered) {
                        pipeline_stats->record(screen_share::diagnostics::PipelineStage::Render);
                    }
                    pipeline_stats->record_duration(
                        screen_share::diagnostics::PipelineStage::Render,
                        static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - render_started)
                                .count()));

                    if (screen_publisher->snapshot().state ==
                        screen_share::media::LiveKitPublishState::Publishing) {
                        screen_share::media::BgraVideoFrame video_frame;
                        if (frame_reader->read(frame, video_frame) ==
                            screen_share::media::D3D11ReadbackResult::Read) {
                            static_cast<void>(screen_publisher->submit(std::move(video_frame)));
                        } else {
                            readback_failures->fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    pipeline_stats->record_duration(
                        screen_share::diagnostics::PipelineStage::Capture,
                        static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - capture_started)
                                .count()));
                });
            status->setText(capture_start_message(result));

            if (result == screen_share::capture::WgcCaptureStartResult::Started) {
                *capture_active_in_ui = true;
                source_combo->setEnabled(false);
                refresh_button->setEnabled(false);
                host_button->setText(QStringLiteral("停止捕获"));
            }
        });
    QObject::connect(
        publish_button,
        &QPushButton::clicked,
        status,
        [capture_session,
         latest_frame,
         screen_publisher,
         server_url,
         access_token,
         publish_button,
         status]() {
            const auto current = screen_publisher->snapshot();
            if (current.state == screen_share::media::LiveKitPublishState::Connecting ||
                current.state == screen_share::media::LiveKitPublishState::Publishing) {
                screen_publisher->stop();
                publish_button->setText(QStringLiteral("开始发布"));
                status->setText(QStringLiteral("LiveKit 屏幕发布已停止，捕获与预览继续。"));
                return;
            }

            const auto width = latest_frame->width.load(std::memory_order_relaxed);
            const auto height = latest_frame->height.load(std::memory_order_relaxed);
            if (!capture_session->running() || width == 0U || height == 0U) {
                status->setText(QStringLiteral("请先开始捕获并等待首帧。"));
                return;
            }

            const screen_share::media::LiveKitPublishConfig configuration{
                .server_url = server_url->text().trimmed().toStdString(),
                .access_token = access_token->text().trimmed().toStdString(),
                .track_name = "screen-share",
            };
            const auto result = screen_publisher->start(configuration, width, height);
            status->setText(publish_start_message(result));
            if (result == screen_share::media::LiveKitPublishStartResult::Starting ||
                result == screen_share::media::LiveKitPublishStartResult::AlreadyActive) {
                publish_button->setText(QStringLiteral("停止发布"));
            }
        });
    QObject::connect(
        join_button,
        &QPushButton::clicked,
        status,
        [capture_session,
         pipeline_stats,
         pipeline_rate,
         preview_renderer,
         frame_reader,
         screen_publisher,
         screen_subscriber,
         capture_active_in_ui,
         source_combo,
         refresh_button,
         host_button,
         publish_button,
         join_button,
         server_url,
         access_token,
         publisher_identity,
         status]() {
            const auto current = screen_subscriber->snapshot();
            if (current.state == screen_share::media::LiveKitSubscribeState::Connecting ||
                current.state == screen_share::media::LiveKitSubscribeState::Watching) {
                screen_subscriber->stop();
                preview_renderer->reset();
                join_button->setText(QStringLiteral("加入观看"));
                status->setText(QStringLiteral("已离开 LiveKit 观看连接。"));
                return;
            }

            capture_session->stop();
            screen_publisher->stop();
            frame_reader->reset();
            preview_renderer->reset();
            pipeline_stats->reset();
            pipeline_rate->reset();
            *capture_active_in_ui = false;
            source_combo->setEnabled(source_combo->count() > 0);
            refresh_button->setEnabled(true);
            host_button->setText(QStringLiteral("开始捕获"));
            publish_button->setText(QStringLiteral("开始发布"));

            const screen_share::media::LiveKitSubscribeConfig configuration{
                .server_url = server_url->text().trimmed().toStdString(),
                .access_token = access_token->text().trimmed().toStdString(),
                .publisher_identity = publisher_identity->text().trimmed().toStdString(),
                .track_name = "screen-share",
            };
            const auto result = screen_subscriber->start(configuration);
            status->setText(subscribe_start_message(result));
            if (result == screen_share::media::LiveKitSubscribeStartResult::Starting ||
                result == screen_share::media::LiveKitSubscribeStartResult::AlreadyActive) {
                join_button->setText(QStringLiteral("停止观看"));
            }
        });
    QObject::connect(
        validation_button,
        &QPushButton::clicked,
        validation_status,
        [validation_recorder,
         screen_publisher,
         screen_subscriber,
         validation_button,
         validation_status,
         sfu_version,
         upstream_mbps,
         downstream_mbps,
         rtt_milliseconds,
         baseline_packet_loss_percent,
         network_baseline_confirmed,
         show_validation_result]() {
            if (validation_recorder->active()) {
                show_validation_result(validation_recorder->stop_and_write());
                return;
            }

            const auto subscribe_snapshot = screen_subscriber->snapshot();
            bool upstream_valid = false;
            bool downstream_valid = false;
            bool rtt_valid = false;
            bool loss_valid = false;
            const double measured_upstream = upstream_mbps->text().trimmed().toDouble(
                &upstream_valid);
            const double measured_downstream = downstream_mbps->text().trimmed().toDouble(
                &downstream_valid);
            const double measured_rtt = rtt_milliseconds->text().trimmed().toDouble(&rtt_valid);
            const double measured_loss_percent =
                baseline_packet_loss_percent->text().trimmed().toDouble(&loss_valid);
            if (sfu_version->text().trimmed().isEmpty() || !upstream_valid ||
                !downstream_valid || !rtt_valid || !loss_valid || measured_upstream <= 0.0 ||
                measured_downstream <= 0.0 || measured_rtt < 0.0 ||
                measured_loss_percent < 0.0 || measured_loss_percent > 100.0 ||
                !network_baseline_confirmed->isChecked()) {
                validation_status->setText(
                    QStringLiteral("请填写并确认本次实测的 SFU 版本、上/下行、RTT 和基线丢包。"));
                return;
            }
            const screen_share::desktop::ValidationEnvironment environment{
                .sfu_version = sfu_version->text().trimmed(),
                .upstream_mbps = measured_upstream,
                .downstream_mbps = measured_downstream,
                .rtt_milliseconds = measured_rtt,
                .baseline_packet_loss_ratio = measured_loss_percent / 100.0,
            };
            if (subscribe_snapshot.state ==
                screen_share::media::LiveKitSubscribeState::Watching) {
                validation_recorder->start(screen_share::desktop::ValidationRole::Viewer,
                                           environment);
            } else if (screen_publisher->snapshot().state ==
                       screen_share::media::LiveKitPublishState::Publishing) {
                validation_recorder->start(screen_share::desktop::ValidationRole::Host,
                                           environment);
            } else {
                validation_status->setText(
                    QStringLiteral("请先成功发布或加入观看，再开始验收记录。"));
                return;
            }

            validation_button->setText(QStringLiteral("停止并保存验收记录"));
            show_validation_result({});
        });

    auto* remote_render_timer = new QTimer(central_widget);
    remote_render_timer->setInterval(8);
    remote_render_timer->setTimerType(Qt::PreciseTimer);
    QObject::connect(
        remote_render_timer,
        &QTimer::timeout,
        preview_widget,
        [pipeline_stats, preview_renderer, screen_subscriber]() {
            screen_share::media::BgraVideoFrame frame;
            if (!screen_subscriber->take_latest(frame)) {
                return;
            }
            if (frame.width > std::numeric_limits<std::uint32_t>::max() / 4U) {
                return;
            }

            const screen_share::renderer::BgraFrameView frame_view{
                .data = frame.pixels.data(),
                .data_size = frame.pixels.size(),
                .width = frame.width,
                .height = frame.height,
                .row_pitch = frame.width * 4U,
            };
            const auto render_started = std::chrono::steady_clock::now();
            if (preview_renderer->render(frame_view) ==
                screen_share::renderer::PreviewRenderResult::Rendered) {
                pipeline_stats->record(screen_share::diagnostics::PipelineStage::Render);
            }
            pipeline_stats->record_duration(
                screen_share::diagnostics::PipelineStage::Render,
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - render_started)
                        .count()));
        });
    remote_render_timer->start();

    auto* frame_status_timer = new QTimer(central_widget);
    frame_status_timer->setInterval(500);
    QObject::connect(
        frame_status_timer,
        &QTimer::timeout,
        status,
        [capture_session,
         latest_frame,
         frame_rate,
         pipeline_stats,
         pipeline_rate,
         preview_renderer,
         frame_reader,
         screen_publisher,
         screen_subscriber,
         readback_failures,
         capture_active_in_ui,
         source_combo,
         refresh_button,
         host_button,
         publish_button,
         join_button,
         record_validation,
         status]() {
            if (*capture_active_in_ui && !capture_session->running()) {
                capture_session->stop();
                screen_publisher->stop();
                frame_reader->reset();
                preview_renderer->reset();
                *capture_active_in_ui = false;
                source_combo->setEnabled(source_combo->count() > 0);
                refresh_button->setEnabled(true);
                host_button->setText(QStringLiteral("开始捕获"));
                publish_button->setText(QStringLiteral("开始发布"));
                status->setText(QStringLiteral("捕获源已关闭，请刷新后重试。"));
                return;
            }

            const auto subscribe_snapshot = screen_subscriber->snapshot();
            if (subscribe_snapshot.state != screen_share::media::LiveKitSubscribeState::Idle) {
                pipeline_stats->set_count(
                    screen_share::diagnostics::PipelineStage::Receive,
                    subscribe_snapshot.stats_samples > 0U
                        ? subscribe_snapshot.received_frames
                        : subscribe_snapshot.callback_frames);
                pipeline_stats->set_count(
                    screen_share::diagnostics::PipelineStage::Decode,
                    subscribe_snapshot.stats_samples > 0U
                        ? subscribe_snapshot.decoded_frames
                        : subscribe_snapshot.callback_frames);
                pipeline_stats->set_duration(
                    screen_share::diagnostics::PipelineStage::Receive,
                    subscribe_snapshot.total_receive_delay_nanoseconds,
                    subscribe_snapshot.receive_delay_samples);
                pipeline_stats->set_duration(
                    screen_share::diagnostics::PipelineStage::Decode,
                    subscribe_snapshot.total_decode_duration_nanoseconds,
                    subscribe_snapshot.decoded_frames);
                const auto viewer_pipeline = pipeline_stats->snapshot();
                const auto viewer_rates =
                    pipeline_rate->sample(viewer_pipeline, steady_timestamp_100ns());
                switch (subscribe_snapshot.state) {
                    case screen_share::media::LiveKitSubscribeState::Idle:
                        break;
                    case screen_share::media::LiveKitSubscribeState::Connecting:
                        status->setText(QStringLiteral("正在连接 LiveKit 观看房间…"));
                        break;
                    case screen_share::media::LiveKitSubscribeState::Watching: {
                        status->setText(
                            QStringLiteral("软件内观看 %1×%2 · 六段 fps C/E/S/Rx/D/R %3/%4/%5/%6/%7/%8 · decode %9 ms · render %10 ms · %11 Mbps · 丢包 %12% · 队列丢弃 %13")
                                .arg(subscribe_snapshot.width)
                                .arg(subscribe_snapshot.height)
                                .arg(viewer_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Capture),
                                     0,
                                     'f',
                                     1)
                                .arg(viewer_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Encode),
                                     0,
                                     'f',
                                     1)
                                .arg(viewer_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Send),
                                     0,
                                     'f',
                                     1)
                                .arg(viewer_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Receive),
                                     0,
                                     'f',
                                     1)
                                .arg(viewer_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Decode),
                                     0,
                                     'f',
                                     1)
                                .arg(viewer_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Render),
                                     0,
                                     'f',
                                     1)
                                .arg(viewer_pipeline.average_duration_milliseconds(
                                         screen_share::diagnostics::PipelineStage::Decode),
                                     0,
                                     'f',
                                     2)
                                .arg(viewer_pipeline.average_duration_milliseconds(
                                         screen_share::diagnostics::PipelineStage::Render),
                                     0,
                                     'f',
                                     2)
                                .arg(static_cast<double>(subscribe_snapshot.bitrate_bps) /
                                         1'000'000.0,
                                     0,
                                     'f',
                                     2)
                                .arg(subscribe_snapshot.packet_loss_ratio * 100.0, 0, 'f', 2)
                                .arg(static_cast<qulonglong>(
                                    subscribe_snapshot.dropped_frames)));
                        break;
                    }
                    case screen_share::media::LiveKitSubscribeState::Failed:
                        status->setText(subscribe_error_message(subscribe_snapshot.error));
                        join_button->setText(QStringLiteral("重新加入"));
                        break;
                }
                screen_share::desktop::ValidationSampleInput validation_sample;
                validation_sample.rates = viewer_rates;
                validation_sample.pipeline = viewer_pipeline;
                validation_sample.width = subscribe_snapshot.width;
                validation_sample.height = subscribe_snapshot.height;
                validation_sample.bitrate_bps = subscribe_snapshot.bitrate_bps;
                validation_sample.packet_loss_ratio = subscribe_snapshot.packet_loss_ratio;
                validation_sample.dropped_frames = subscribe_snapshot.dropped_frames;
                validation_sample.media_active =
                    subscribe_snapshot.state ==
                    screen_share::media::LiveKitSubscribeState::Watching;
                record_validation(screen_share::desktop::ValidationRole::Viewer,
                                  validation_sample);
                return;
            }

            if (!*capture_active_in_ui) {
                return;
            }

            const auto rate_sample = frame_rate->sample();
            if (rate_sample.total_frames == 0) {
                return;
            }

            const auto width = latest_frame->width.load(std::memory_order_relaxed);
            const auto height = latest_frame->height.load(std::memory_order_relaxed);
            const auto publish_snapshot = screen_publisher->snapshot();
            pipeline_stats->set_count(
                screen_share::diagnostics::PipelineStage::Encode,
                publish_snapshot.encoded_frames);
            pipeline_stats->set_count(
                screen_share::diagnostics::PipelineStage::Send,
                publish_snapshot.sent_frames);
            pipeline_stats->set_duration(
                screen_share::diagnostics::PipelineStage::Encode,
                publish_snapshot.total_encode_duration_nanoseconds,
                publish_snapshot.encoded_frames);
            pipeline_stats->set_duration(
                screen_share::diagnostics::PipelineStage::Send,
                publish_snapshot.total_send_delay_nanoseconds,
                publish_snapshot.packets_sent);
            const auto pipeline_snapshot = pipeline_stats->snapshot();
            const auto pipeline_rates =
                pipeline_rate->sample(pipeline_snapshot, steady_timestamp_100ns());
            QString publish_status;
            switch (publish_snapshot.state) {
                case screen_share::media::LiveKitPublishState::Idle:
                    publish_status = QStringLiteral("未发布");
                    break;
                case screen_share::media::LiveKitPublishState::Connecting:
                    publish_status = QStringLiteral("LiveKit 连接中");
                    break;
                case screen_share::media::LiveKitPublishState::Publishing:
                    publish_status =
                        QStringLiteral("SDK 发布 %1/%2 帧 · 丢弃 %3")
                            .arg(static_cast<qulonglong>(publish_snapshot.published_frames))
                            .arg(static_cast<qulonglong>(publish_snapshot.accepted_frames))
                            .arg(static_cast<qulonglong>(publish_snapshot.dropped_frames));
                    break;
                case screen_share::media::LiveKitPublishState::Failed:
                    publish_status = publish_error_message(publish_snapshot.error);
                    publish_button->setText(QStringLiteral("重新发布"));
                    break;
            }
            status->setText(QStringLiteral("正在捕获 %1×%2 · 六段 fps C/E/S/Rx/D/R %3/%4/%5/%6/%7/%8 · capture %9 ms · encode %10 ms · %11 Mbps · 丢包 %12% · %13 · 读回失败 %14")
                                .arg(width)
                                .arg(height)
                                .arg(pipeline_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Capture),
                                     0,
                                     'f',
                                     1)
                                .arg(pipeline_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Encode),
                                     0,
                                     'f',
                                     1)
                                .arg(pipeline_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Send),
                                     0,
                                     'f',
                                     1)
                                .arg(pipeline_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Receive),
                                     0,
                                     'f',
                                     1)
                                .arg(pipeline_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Decode),
                                     0,
                                     'f',
                                     1)
                                .arg(pipeline_rates.rate(
                                         screen_share::diagnostics::PipelineStage::Render),
                                     0,
                                     'f',
                                     1)
                                .arg(pipeline_snapshot.average_duration_milliseconds(
                                         screen_share::diagnostics::PipelineStage::Capture),
                                     0,
                                     'f',
                                     2)
                                .arg(pipeline_snapshot.average_duration_milliseconds(
                                         screen_share::diagnostics::PipelineStage::Encode),
                                     0,
                                     'f',
                                     2)
                                .arg(static_cast<double>(publish_snapshot.bitrate_bps) /
                                         1'000'000.0,
                                     0,
                                     'f',
                                     2)
                                .arg(publish_snapshot.packet_loss_ratio * 100.0, 0, 'f', 2)
                                .arg(publish_status)
                                .arg(static_cast<qulonglong>(
                                    readback_failures->load(std::memory_order_relaxed))));
            screen_share::desktop::ValidationSampleInput validation_sample;
            validation_sample.rates = pipeline_rates;
            validation_sample.pipeline = pipeline_snapshot;
            validation_sample.width = width;
            validation_sample.height = height;
            validation_sample.bitrate_bps = publish_snapshot.bitrate_bps;
            validation_sample.packet_loss_ratio = publish_snapshot.packet_loss_ratio;
            validation_sample.dropped_frames = publish_snapshot.dropped_frames;
            validation_sample.media_active =
                publish_snapshot.state == screen_share::media::LiveKitPublishState::Publishing;
            record_validation(screen_share::desktop::ValidationRole::Host, validation_sample);
        });
    frame_status_timer->start();

    layout->addWidget(title);
    layout->addWidget(summary);
    layout->addSpacing(12);
    layout->addWidget(source_group);
    layout->addWidget(preview_group, 1);
    layout->addWidget(publish_group);
    layout->addWidget(validation_environment_group);
    layout->addWidget(host_button);
    layout->addWidget(join_button);
    layout->addWidget(validation_button);
    layout->addWidget(validation_status);
    layout->addStretch();
    layout->addWidget(status);

    window.setCentralWidget(central_widget);
    refresh_sources();
    window.show();

    const auto exit_code = application.exec();
    capture_session->stop();
    screen_publisher->stop();
    screen_subscriber->stop();
    if (validation_recorder->active()) {
        static_cast<void>(validation_recorder->stop_and_write());
    }
    frame_reader->reset();
    preview_renderer->reset();
    return exit_code;
}
