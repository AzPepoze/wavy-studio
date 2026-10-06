// Qt's debugging banner runs before main and would make smoke tests noisy.
#ifdef QT_QML_DEBUG
#undef QT_QML_DEBUG
#endif
#include "EffectsController.hpp"
#include "EngineController.hpp"
#include "RecordController.hpp"
#include "SnapshotPublisher.hpp"
#include "TimelineModel.hpp"
#include "UserSettings.hpp"
#include "WaveformBridge.hpp"
#include "WaveformItem.hpp"
#include "core/Log.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <string_view>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--smoke-test")
            wavy::log::setSink([](std::string_view) {});
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
    app.setOrganizationName("Wavy Studio");
    app.setApplicationName("Wavy Studio");
    QQuickStyle::setStyle("Basic");
    const bool smoke = app.arguments().contains("--smoke-test");
    if (smoke)
        wavy::log::info("app", "Running --smoke-test");
    TimelineModel timeline;
    EffectsController effects(timeline);
    EngineController audio(timeline);
    RecordController recorder(timeline, audio);
    SnapshotPublisher publisher(timeline, audio.engine().mixer());
    publisher.setEffectsController(effects);
    UserSettings userSettings;
    WaveformBridge waveformBridge;
    waveformBridge.setLibrary(publisher.sourceLibrary());
    QObject::connect(&publisher, &SnapshotPublisher::sourceReady, &waveformBridge,
                     &WaveformBridge::sourceReady);
    qmlRegisterType<WaveformItem>("Wavy.Waveform", 1, 0, "WaveformItem");
    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty("audioEngine", &audio);
    qml.rootContext()->setContextProperty("recorder", &recorder);
    qml.rootContext()->setContextProperty("timelineModel", &timeline);
    qml.rootContext()->setContextProperty("effects", &effects);
    qml.rootContext()->setContextProperty("userSettings", &userSettings);
    qml.rootContext()->setContextProperty("waveformBridge", &waveformBridge);
    qml.load(QUrl(QStringLiteral("qrc:/ui/main.qml")));
    if (qml.rootObjects().isEmpty())
        return 1;
    if (smoke)
        QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
