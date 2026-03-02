#include "UiAutomationProxyServer.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QDebug>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QHostAddress> // 需要包含此头文件

int main(int argc, char *argv[])
{
    // 1. 初始化应用程序
    QApplication app(argc, argv);

    // 2. 初始化 QML 引擎
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { qWarning() << "Failed to create QML root object from qrc:/Main.qml"; },
        Qt::QueuedConnection);
    
    
    // !!! 修正: 使用 '.' 操作符 !!!
     engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    
    // !!! 修正: 删除多余的 'ui'，并清理语法 !!!
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    // 4. 获取根对象并初始化 Proxy
    QObject *root = engine.rootObjects().first();
    UiAutomationProxyServer proxy;
    proxy.useDefaultQtHandler(root);
    proxy.start(12345, QHostAddress::LocalHost, QStringLiteral("demo-token"));

    // 5. 处理事件循环
    return app.exec();
}