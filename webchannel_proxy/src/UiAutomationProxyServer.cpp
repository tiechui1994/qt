#include "UiAutomationProxyServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QWebChannel>
#include <QWebChannelAbstractTransport>
#include <QWebSocket>
#include <QWebSocketServer>

class UiAutomationProxyServer::SocketTransport : public QWebChannelAbstractTransport {
    Q_OBJECT
public:
    explicit SocketTransport(QWebSocket *socket, QObject *parent = nullptr)
        : QWebChannelAbstractTransport(parent), m_socket(socket) {}

    void sendMessage(const QJsonObject &message) override {
        if (!m_socket) {
            return;
        }
        const auto payload = QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact));
        m_socket->sendTextMessage(payload);
    }

    void dispatchMessage(const QJsonObject &message) {
        emit messageReceived(message, this);
    }

private:
    QPointer<QWebSocket> m_socket;
};

UiAutomationBridge::UiAutomationBridge(QObject *parent)
    : QObject(parent) {}

void UiAutomationBridge::setHandler(UiAutomationHandler *handler) {
    m_handler = handler;
}

UiAutomationHandler *UiAutomationBridge::handler() const {
    return m_handler;
}

QJsonObject UiAutomationBridge::resolve(const QJsonObject &target) const {
    if (!m_handler) {
        return fail(QStringLiteral("Handler is not configured"));
    }
    QString error;
    const auto result = m_handler->resolve(target, &error);
    return error.isEmpty() ? ok(result) : fail(error);
}

QJsonObject UiAutomationBridge::executeAction(const QString &action, const QJsonObject &target, const QJsonValue &value) const {
    if (!m_handler) {
        return fail(QStringLiteral("Handler is not configured"));
    }
    QString error;
    const auto result = m_handler->executeAction(action, target, value, &error);
    return error.isEmpty() ? ok(result) : fail(error);
}

QJsonObject UiAutomationBridge::readProperty(const QJsonObject &target, const QString &propertyName) const {
    if (!m_handler) {
        return fail(QStringLiteral("Handler is not configured"));
    }
    QString error;
    const auto result = m_handler->readProperty(target, propertyName, &error);
    return error.isEmpty() ? ok(result) : fail(error);
}

QJsonObject UiAutomationBridge::screenshot(const QString &path) const {
    if (!m_handler) {
        return fail(QStringLiteral("Handler is not configured"));
    }
    QString error;
    const auto result = m_handler->screenshot(path, &error);
    return error.isEmpty() ? ok(result) : fail(error);
}

QJsonObject UiAutomationBridge::dumpTree() const {
    if (!m_handler) {
        return fail(QStringLiteral("Handler is not configured"));
    }
    QString error;
    const auto result = m_handler->dumpTree(&error);
    return error.isEmpty() ? ok(result) : fail(error);
}

QJsonObject UiAutomationBridge::ok(const QJsonValue &result) const {
    QJsonObject out;
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("result"), result);
    return out;
}

QJsonObject UiAutomationBridge::fail(const QString &error) const {
    QJsonObject out;
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), error);
    return out;
}

UiAutomationProxyServer::UiAutomationProxyServer(QObject *parent)
    : QObject(parent),
      m_server(new QWebSocketServer(QStringLiteral("UiAutomationProxyServer"), QWebSocketServer::NonSecureMode, this)),
      m_channel(new QWebChannel(this)),
      m_bridge(new UiAutomationBridge(this)) {
    m_channel->registerObject(QStringLiteral("qtProxyBridge"), m_bridge);
    connect(m_server, &QWebSocketServer::newConnection, this, &UiAutomationProxyServer::onNewConnection);
}

UiAutomationProxyServer::~UiAutomationProxyServer() {
    stop();
}

void UiAutomationProxyServer::setHandler(UiAutomationHandler *handler) {
    m_defaultHandler.reset();
    m_bridge->setHandler(handler);
}

void UiAutomationProxyServer::useDefaultQtHandler(QObject *rootObject) {
    m_defaultHandler = std::make_unique<QtGenericUiAutomationHandler>(rootObject);
    m_bridge->setHandler(m_defaultHandler.get());
}

void UiAutomationProxyServer::useDefaultQmlHandler(QObject *rootObject) {
    m_defaultHandler = std::make_unique<QtQmlUiAutomationHandler>(rootObject);
    m_bridge->setHandler(m_defaultHandler.get());
}

UiAutomationBridge *UiAutomationProxyServer::bridge() const {
    return m_bridge;
}

bool UiAutomationProxyServer::start(quint16 port, const QHostAddress &address, const QString &token) {
    m_token = token;
    if (m_server->isListening()) {
        m_server->close();
    }
    return m_server->listen(address, port);
}

void UiAutomationProxyServer::stop() {
    const auto sockets = m_transports.keys();
    for (QWebSocket *socket : sockets) {
        if (socket) {
            socket->close();
            socket->deleteLater();
        }
    }
    qDeleteAll(m_transports);
    m_transports.clear();
    if (m_server->isListening()) {
        m_server->close();
    }
}

bool UiAutomationProxyServer::isListening() const {
    return m_server->isListening();
}

quint16 UiAutomationProxyServer::serverPort() const {
    return m_server->serverPort();
}

void UiAutomationProxyServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        auto *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }
        auto *transport = new SocketTransport(socket, this);
        m_channel->connectTo(transport);
        m_transports.insert(socket, transport);

        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) {
            onSocketMessage(socket, text);
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
            onSocketDisconnected(socket);
        });
    }
}

void UiAutomationProxyServer::onSocketDisconnected(QWebSocket *socket) {
    auto *transport = m_transports.take(socket);
    if (transport) {
        m_channel->disconnectFrom(transport);
        transport->deleteLater();
    }
    if (socket) {
        socket->deleteLater();
    }
}

void UiAutomationProxyServer::onSocketMessage(QWebSocket *socket, const QString &textMessage) {
    const auto doc = QJsonDocument::fromJson(textMessage.toUtf8());
    if (!doc.isObject()) {
        return;
    }
    const auto obj = doc.object();
    if (!obj.contains(QStringLiteral("method"))) {
        auto *transport = m_transports.value(socket, nullptr);
        if (transport) {
            transport->dispatchMessage(obj);
        }
        return;
    }

    const int id = obj.value(QStringLiteral("id")).toInt(-1);
    const QString method = obj.value(QStringLiteral("method")).toString();
    const auto params = obj.value(QStringLiteral("params")).toObject();
    if (id < 0 || method.isEmpty()) {
        return;
    }
    if (!m_token.isEmpty()) {
        const QString token = obj.value(QStringLiteral("token")).toString();
        if (token != m_token) {
            reply(socket, id, QJsonValue(), QStringLiteral("unauthorized"));
            return;
        }
    }

    QJsonObject callResult;
    if (method == QStringLiteral("resolve")) {
        callResult = m_bridge->resolve(params.value(QStringLiteral("target")).toObject());
    } else if (method == QStringLiteral("execute_action")) {
        callResult = m_bridge->executeAction(
            params.value(QStringLiteral("action")).toString(),
            params.value(QStringLiteral("target")).toObject(),
            params.value(QStringLiteral("value")));
    } else if (method == QStringLiteral("read_property")) {
        callResult = m_bridge->readProperty(
            params.value(QStringLiteral("target")).toObject(),
            params.value(QStringLiteral("property")).toString());
    } else if (method == QStringLiteral("screenshot")) {
        callResult = m_bridge->screenshot(params.value(QStringLiteral("path")).toString());
    } else if (method == QStringLiteral("dump_tree")) {
        callResult = m_bridge->dumpTree();
    } else {
        reply(socket, id, QJsonValue(), QStringLiteral("unknown method"));
        return;
    }

    const bool ok = callResult.value(QStringLiteral("ok")).toBool(false);
    if (!ok) {
        reply(socket, id, QJsonValue(), callResult.value(QStringLiteral("error")).toString());
        return;
    }
    reply(socket, id, callResult.value(QStringLiteral("result")));
}

void UiAutomationProxyServer::reply(QWebSocket *socket, int id, const QJsonValue &result, const QString &error) const {
    if (!socket) {
        return;
    }
    QJsonObject out;
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("result"), result);
    if (!error.isEmpty()) {
        out.insert(QStringLiteral("error"), error);
    } else {
        out.insert(QStringLiteral("error"), QJsonValue());
    }
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
}

#include "UiAutomationProxyServer.moc"