#include "screen_share/capture/capture_source.hpp"
#include "screen_share/core/session_policy.hpp"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QString>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

namespace {

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

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("Screen Share"));
    QApplication::setOrganizationName(QStringLiteral("Screen Share Contributors"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Screen Share · M1"));
    window.resize(640, 480);

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

    auto* status = new QLabel(QStringLiteral("M1-01：请选择屏幕或窗口"), central_widget);
    status->setWordWrap(true);
    auto* host_button = new QPushButton(QStringLiteral("确认捕获源"), central_widget);
    auto* join_button = new QPushButton(QStringLiteral("加入观看"), central_widget);

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
    QObject::connect(host_button, &QPushButton::clicked, status, [source_combo, status]() {
        if (source_combo->currentIndex() < 0) {
            status->setText(QStringLiteral("请先刷新并选择一个可分享源。"));
            return;
        }

        status->setText(
            QStringLiteral("已确认：%1。D3D11 帧捕获将在 M1-02 接入。")
                .arg(source_combo->currentText()));
    });
    QObject::connect(join_button, &QPushButton::clicked, status, [status]() {
        status->setText(QStringLiteral("房间加入将在 M2 阶段启用"));
    });

    layout->addWidget(title);
    layout->addWidget(summary);
    layout->addSpacing(12);
    layout->addWidget(source_group);
    layout->addWidget(host_button);
    layout->addWidget(join_button);
    layout->addStretch();
    layout->addWidget(status);

    window.setCentralWidget(central_widget);
    refresh_sources();
    window.show();

    return application.exec();
}
