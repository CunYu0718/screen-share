#include "screen_share/core/session_policy.hpp"

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("Screen Share"));
    QApplication::setOrganizationName(QStringLiteral("Screen Share Contributors"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Screen Share · M0"));
    window.resize(560, 360);

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

    auto* status = new QLabel(QStringLiteral("M0 工程基线：媒体功能尚未启用"), central_widget);
    auto* host_button = new QPushButton(QStringLiteral("发起共享"), central_widget);
    auto* join_button = new QPushButton(QStringLiteral("加入观看"), central_widget);

    QObject::connect(host_button, &QPushButton::clicked, status, [status]() {
        status->setText(QStringLiteral("屏幕捕获将在 M1 阶段启用"));
    });
    QObject::connect(join_button, &QPushButton::clicked, status, [status]() {
        status->setText(QStringLiteral("房间加入将在 M2 阶段启用"));
    });

    layout->addWidget(title);
    layout->addWidget(summary);
    layout->addSpacing(12);
    layout->addWidget(host_button);
    layout->addWidget(join_button);
    layout->addStretch();
    layout->addWidget(status);

    window.setCentralWidget(central_widget);
    window.show();

    return application.exec();
}
