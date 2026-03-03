#include <QApplication>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [](QObject *obj, const QUrl &url) {
            if (!obj) {
                qCritical() << "Failed to create QML root object from" << url;
            }
        },
        Qt::QueuedConnection);
    qInfo() << "Loading QML from qrc:/ui/Main.qml";
    engine.load(QUrl(QStringLiteral("qrc:/ui/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qWarning() << "Fallback loading qrc:/Main.qml";
        engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    }
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "No root objects loaded for qml_demo";
        return 1;
    }

    return app.exec();
}
