#pragma once

#include <QObject>
#include <QHostAddress>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <memory>

class QWebSocket;
class QWebSocketServer;
class QWebChannel;
class QWebChannelAbstractTransport;

class UiAutomationHandler {
public:
    virtual ~UiAutomationHandler() = default;
    virtual QJsonValue resolve(const QJsonObject &target, QString *error) = 0;
    virtual QJsonValue executeAction(const QString &action, const QJsonObject &target, const QJsonValue &value, QString *error) = 0;
    virtual QJsonValue readProperty(const QJsonObject &target, const QString &propertyName, QString *error) = 0;
    virtual QJsonValue screenshot(const QString &path, QString *error) = 0;
    virtual QJsonValue dumpTree(QString *error) = 0;
};

class QtGenericUiAutomationHandler final : public UiAutomationHandler {
public:
    explicit QtGenericUiAutomationHandler(QObject *rootObject = nullptr);

    void setRootObject(QObject *rootObject);
    QObject *rootObject() const;

    QJsonValue resolve(const QJsonObject &target, QString *error) override;
    QJsonValue executeAction(const QString &action, const QJsonObject &target, const QJsonValue &value, QString *error) override;
    QJsonValue readProperty(const QJsonObject &target, const QString &propertyName, QString *error) override;
    QJsonValue screenshot(const QString &path, QString *error) override;
    QJsonValue dumpTree(QString *error) override;

private:
    QObject *findTarget(const QJsonObject &target, QString *error) const;
    QObject *findByObjectNameLike(const QString &value) const;
    QObject *findByTextLike(const QString &value) const;
    bool clickObject(QObject *obj) const;
    bool setChecked(QObject *obj, bool checked) const;
    bool setCurrentText(QObject *obj, const QString &value) const;
    bool setCurrentIndex(QObject *obj, int index) const;
    bool closeObject(QObject *obj) const;
    bool setTextValue(QObject *obj, const QString &value) const;
    QObject *rootRequired(QString *error) const;

    QObject *m_root = nullptr;
};

class UiAutomationBridge : public QObject {
    Q_OBJECT
public:
    explicit UiAutomationBridge(QObject *parent = nullptr);

    void setHandler(UiAutomationHandler *handler);
    UiAutomationHandler *handler() const;

    Q_INVOKABLE QJsonObject resolve(const QJsonObject &target) const;
    Q_INVOKABLE QJsonObject executeAction(const QString &action, const QJsonObject &target, const QJsonValue &value) const;
    Q_INVOKABLE QJsonObject readProperty(const QJsonObject &target, const QString &propertyName) const;
    Q_INVOKABLE QJsonObject screenshot(const QString &path) const;
    Q_INVOKABLE QJsonObject dumpTree() const;

private:
    QJsonObject ok(const QJsonValue &result) const;
    QJsonObject fail(const QString &error) const;
    UiAutomationHandler *m_handler = nullptr;
};

class UiAutomationProxyServer : public QObject {
    Q_OBJECT
public:
    explicit UiAutomationProxyServer(QObject *parent = nullptr);
    ~UiAutomationProxyServer() override;

    void setHandler(UiAutomationHandler *handler);
    void useDefaultQtHandler(QObject *rootObject);
    UiAutomationBridge *bridge() const;

    bool start(quint16 port, const QHostAddress &address = QHostAddress::LocalHost, const QString &token = QString());
    void stop();
    bool isListening() const;
    quint16 serverPort() const;

private:
    class SocketTransport;
    void onNewConnection();
    void onSocketDisconnected(QWebSocket *socket);
    void onSocketMessage(QWebSocket *socket, const QString &textMessage);
    void reply(QWebSocket *socket, int id, const QJsonValue &result, const QString &error = QString()) const;

    QString m_token;
    QWebSocketServer *m_server = nullptr;
    QWebChannel *m_channel = nullptr;
    UiAutomationBridge *m_bridge = nullptr;
    std::unique_ptr<QtGenericUiAutomationHandler> m_defaultHandler;
    QHash<QWebSocket *, SocketTransport *> m_transports;
};
