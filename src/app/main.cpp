#include "EngineController.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <string_view>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--smoke-test")
            qputenv("QT_QPA_PLATFORMTHEME", "");
    QGuiApplication app(argc, argv);
    app.setApplicationName("Wavy Studio");
    QQuickStyle::setStyle("Basic");
    const bool smoke = app.arguments().contains("--smoke-test");
    EngineController audio;
    if (smoke && !audio.start())
        return 1;
    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty("audioEngine", &audio);
    qml.load(QUrl(QStringLiteral("qrc:/ui/main.qml")));
    if (qml.rootObjects().isEmpty())
        return 1;
    if (smoke)
        QTimer::singleShot(100, &app, &QCoreApplication::quit);
    return app.exec();
}
