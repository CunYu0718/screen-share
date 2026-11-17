#include "screen_share/capture/capture_source.hpp"
#include "screen_share/capture/wgc_capture_session.hpp"
#include "screen_share/core/session_policy.hpp"
#include "screen_share/diagnostics/frame_pipeline_stats.hpp"
#include "screen_share/diagnostics/frame_rate_estimator.hpp"
#include "screen_share/renderer/d3d11_preview_renderer.hpp"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
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
#include <cstdint>
#include <memory>

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

}  // namespace

int main(int argc, char* argv[]) {
    const WindowsRuntimeApartment runtime_apartment;
    QApplication application(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("Screen Share"));
    QApplication::setOrganizationName(QStringLiteral("Screen Share Contributors"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Screen Share · M1"));
    window.resize(800, 720);

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

    auto* status = new QLabel(QStringLiteral("M1-03：请选择屏幕或窗口"), central_widget);
    status->setWordWrap(true);
    auto* host_button = new QPushButton(QStringLiteral("开始捕获"), central_widget);
    auto* join_button = new QPushButton(QStringLiteral("加入观看"), central_widget);
    const auto capture_session = std::make_shared<screen_share::capture::WgcCaptureSession>();
    const auto latest_frame = std::make_shared<LatestFrameState>();
    const auto frame_rate = std::make_shared<screen_share::diagnostics::FrameRateEstimator>();
    const auto pipeline_stats =
        std::make_shared<screen_share::diagnostics::FramePipelineStats>();
    const auto preview_handle = reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<void*>(preview_widget->winId()));
    const auto preview_renderer =
        std::make_shared<screen_share::renderer::D3D11PreviewRenderer>(preview_handle);
    const auto capture_active_in_ui = std::make_shared<bool>(false);

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
         preview_renderer,
         capture_active_in_ui,
         source_combo,
         refresh_button,
         host_button,
         status]() {
            if (*capture_active_in_ui) {
                capture_session->stop();
                preview_renderer->reset();
                *capture_active_in_ui = false;
                source_combo->setEnabled(source_combo->count() > 0);
                refresh_button->setEnabled(true);
                host_button->setText(QStringLiteral("开始捕获"));
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
            preview_renderer->reset();

            const auto result = capture_session->start(
                source,
                [latest_frame, frame_rate, pipeline_stats, preview_renderer](
                    const screen_share::capture::D3D11CaptureFrameView& frame) {
                    latest_frame->width.store(frame.width, std::memory_order_relaxed);
                    latest_frame->height.store(frame.height, std::memory_order_relaxed);
                    frame_rate->record(frame.system_relative_time_100ns);
                    pipeline_stats->record(screen_share::diagnostics::PipelineStage::Capture);
                    if (preview_renderer->render(frame) ==
                        screen_share::renderer::PreviewRenderResult::Rendered) {
                        pipeline_stats->record(screen_share::diagnostics::PipelineStage::Render);
                    }
                });
            status->setText(capture_start_message(result));

            if (result == screen_share::capture::WgcCaptureStartResult::Started) {
                *capture_active_in_ui = true;
                source_combo->setEnabled(false);
                refresh_button->setEnabled(false);
                host_button->setText(QStringLiteral("停止捕获"));
            }
        });
    QObject::connect(join_button, &QPushButton::clicked, status, [status]() {
        status->setText(QStringLiteral("房间加入将在 M2 阶段启用"));
    });

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
         preview_renderer,
         capture_active_in_ui,
         source_combo,
         refresh_button,
         host_button,
         status]() {
            if (*capture_active_in_ui && !capture_session->running()) {
                capture_session->stop();
                preview_renderer->reset();
                *capture_active_in_ui = false;
                source_combo->setEnabled(source_combo->count() > 0);
                refresh_button->setEnabled(true);
                host_button->setText(QStringLiteral("开始捕获"));
                status->setText(QStringLiteral("捕获源已关闭，请刷新后重试。"));
                return;
            }

            if (!*capture_active_in_ui) {
                return;
            }

            const auto rate_sample = frame_rate->sample();
            const auto pipeline_snapshot = pipeline_stats->snapshot();
            if (rate_sample.total_frames == 0) {
                return;
            }

            const auto width = latest_frame->width.load(std::memory_order_relaxed);
            const auto height = latest_frame->height.load(std::memory_order_relaxed);
            const auto rendered_frames = pipeline_snapshot.count(
                screen_share::diagnostics::PipelineStage::Render);
            status->setText(QStringLiteral("正在捕获 %1×%2 · capture %3 fps · 预览 %4/%5 帧")
                                .arg(width)
                                .arg(height)
                                .arg(rate_sample.frames_per_second, 0, 'f', 1)
                                .arg(static_cast<qulonglong>(rendered_frames))
                                .arg(static_cast<qulonglong>(rate_sample.total_frames)));
        });
    frame_status_timer->start();

    layout->addWidget(title);
    layout->addWidget(summary);
    layout->addSpacing(12);
    layout->addWidget(source_group);
    layout->addWidget(preview_group, 1);
    layout->addWidget(host_button);
    layout->addWidget(join_button);
    layout->addStretch();
    layout->addWidget(status);

    window.setCentralWidget(central_widget);
    refresh_sources();
    window.show();

    const auto exit_code = application.exec();
    capture_session->stop();
    preview_renderer->reset();
    return exit_code;
}
