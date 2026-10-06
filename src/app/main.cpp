#include "EngineController.hpp"
#include "SnapshotPublisher.hpp"
#include "TimelineModel.hpp"
#include "core/Log.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <string_view>

int main(int argc, char* argv[]) {
    qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext& context, const QString& message) {
            auto level = wavy::log::Level::Info;
            switch (type) {
            case QtDebugMsg:
                level = wavy::log::Level::Debug;
                break;
            case QtWarningMsg:
                level = wavy::log::Level::Warn;
                break;
            case QtCriticalMsg:
            case QtFatalMsg:
                level = wavy::log::Level::Error;
                break;
            case QtInfoMsg:
                break;
            }
            const auto text = message.toUtf8();
            const std::string_view category = context.category ? context.category : "qt";
            wavy::log::write(level, category == "default" ? "qt" : category,
                             std::string_view(text.constData(), text.size()));
        });
    wavy::log::info("app", "Wavy Studio starting");
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--smoke-test")
            qputenv("QT_QPA_PLATFORMTHEME", "");
    QGuiApplication app(argc, argv);
    app.setApplicationName("Wavy Studio");
    QQuickStyle::setStyle("Basic");
    const bool smoke = app.arguments().contains("--smoke-test");
    if (smoke)
        wavy::log::info("app", "Running --smoke-test");
    TimelineModel timeline;
    EngineController audio(timeline);
    SnapshotPublisher publisher(timeline, audio.engine().mixer());
    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty("audioEngine", &audio);
    qml.rootContext()->setContextProperty("timelineModel", &timeline);
    qml.load(QUrl(QStringLiteral("qrc:/ui/main.qml")));
    if (qml.rootObjects().isEmpty())
        return 1;
    if (smoke)
        QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
