#include "UiAutomationProxyServer.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSlider>
#include <QVariantList>
#include <QWebChannel>
#include <QWebChannelAbstractTransport>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QWidget>

namespace {
QString asError(const QString &fallback, QString *error) {
    if (error) {
        *error = fallback;
    }
    return fallback;
}

bool invokeNoArgs(QObject *obj, const char *method) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection);
}

bool invokeSetInt(QObject *obj, const char *method, int value) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection, Q_ARG(int, value));
}

bool invokeSetBool(QObject *obj, const char *method, bool value) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection, Q_ARG(bool, value));
}

bool invokeSetString(QObject *obj, const char *method, const QString &value) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection, Q_ARG(QString, value));
}

QJsonValue toJson(const QVariant &value) {
    return QJsonValue::fromVariant(value);
}
}  // namespace

QtGenericUiAutomationHandler::QtGenericUiAutomationHandler(QObject *rootObject)
    : m_root(rootObject) {}

void QtGenericUiAutomationHandler::setRootObject(QObject *rootObject) {
    m_root = rootObject;
}

QObject *QtGenericUiAutomationHandler::rootObject() const {
    return m_root;
}

QJsonValue QtGenericUiAutomationHandler::resolve(const QJsonObject &target, QString *error) {
    QObject *obj = findTarget(target, error);
    if (!obj) {
        return {};
    }
    QJsonObject out;
    out.insert(QStringLiteral("objectName"), obj->objectName());
    out.insert(QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className()));
    const QVariant text = obj->property("text");
    if (text.isValid()) {
        out.insert(QStringLiteral("text"), toJson(text));
    }
    const QVariant title = obj->property("title");
    if (title.isValid()) {
        out.insert(QStringLiteral("title"), toJson(title));
    }
    const QVariant visible = obj->property("visible");
    if (visible.isValid()) {
        out.insert(QStringLiteral("visible"), visible.toBool());
    }
    return out;
}

QJsonValue QtGenericUiAutomationHandler::executeAction(
    const QString &action,
    const QJsonObject &target,
    const QJsonValue &value,
    QString *error) {
    QObject *root = rootRequired(error);
    if (!root) {
        return {};
    }

    const QString op = action.trimmed().toLower();
    QObject *obj = nullptr;
    if (op != QStringLiteral("multi_select")) {
        if (op == QStringLiteral("close_page") && target.isEmpty()) {
            obj = root;
        } else {
            obj = findTarget(target, error);
            if (!obj) {
                return {};
            }
        }
    }

    if (op == QStringLiteral("click")) {
        if (!clickObject(obj)) {
            asError(QStringLiteral("click not supported by target"), error);
            return {};
        }
    } else if (op == QStringLiteral("input")) {
        if (!setTextValue(obj, value.toString())) {
            asError(QStringLiteral("input requires a text-capable target"), error);
            return {};
        }
    } else if (op == QStringLiteral("upload")) {
        const QString abs = QFileInfo(value.toString()).absoluteFilePath();
        if (!setTextValue(obj, abs)) {
            asError(QStringLiteral("upload requires a text-capable target"), error);
            return {};
        }
    } else if (op == QStringLiteral("select")) {
        if (!setCurrentText(obj, value.toString())) {
            asError(QStringLiteral("select requires currentText/currentIndex support"), error);
            return {};
        }
    } else if (op == QStringLiteral("switch_page")) {
        if (value.isDouble()) {
            if (!setCurrentIndex(obj, value.toInt())) {
                asError(QStringLiteral("switch_page(index) not supported"), error);
                return {};
            }
        } else {
            const QString raw = value.toString();
            bool isInt = false;
            const int idx = raw.toInt(&isInt);
            if (isInt) {
                if (!setCurrentIndex(obj, idx)) {
                    asError(QStringLiteral("switch_page(index) not supported"), error);
                    return {};
                }
            } else if (!setCurrentText(obj, raw)) {
                asError(QStringLiteral("switch_page(text) not supported"), error);
                return {};
            }
        }
    } else if (op == QStringLiteral("slide")) {
        if (auto *slider = qobject_cast<QSlider *>(obj)) {
            slider->setValue(value.toInt());
        } else if (!obj->setProperty("value", value.toVariant())) {
            asError(QStringLiteral("slide requires slider/value target"), error);
            return {};
        }
    } else if (op == QStringLiteral("toggle")) {
        if (value.isBool()) {
            if (!setChecked(obj, value.toBool())) {
                asError(QStringLiteral("toggle(bool) not supported"), error);
                return {};
            }
        } else if (!clickObject(obj)) {
            asError(QStringLiteral("toggle requires click or checked support"), error);
            return {};
        }
    } else if (op == QStringLiteral("single_select")) {
        if (!setChecked(obj, true) && !clickObject(obj)) {
            asError(QStringLiteral("single_select not supported"), error);
            return {};
        }
    } else if (op == QStringLiteral("multi_select")) {
        QVariantList values;
        if (value.isArray()) {
            values = value.toArray().toVariantList();
        } else {
            values.push_back(value.toVariant());
        }
        for (const QVariant &v : values) {
            const QString token = v.toString();
            QObject *item = findByObjectNameLike(token);
            if (!item) {
                item = findByTextLike(token);
            }
            if (!item) {
                asError(QStringLiteral("multi_select option not found: %1").arg(token), error);
                return {};
            }
            if (!setChecked(item, true) && !clickObject(item)) {
                asError(QStringLiteral("multi_select option not checkable: %1").arg(token), error);
                return {};
            }
        }
    } else if (op == QStringLiteral("dropdown_multi_select")) {
        QVariantList values;
        if (value.isArray()) {
            values = value.toArray().toVariantList();
        } else {
            values.push_back(value.toVariant());
        }
        if (auto *list = qobject_cast<QListWidget *>(obj)) {
            for (int i = 0; i < list->count(); ++i) {
                auto *item = list->item(i);
                if (item) {
                    item->setSelected(values.contains(item->text()));
                }
            }
        } else if (auto *combo = qobject_cast<QComboBox *>(obj)) {
            auto *model = combo->model();
            if (!model) {
                asError(QStringLiteral("dropdown_multi_select model is null"), error);
                return {};
            }
            for (int row = 0; row < model->rowCount(); ++row) {
                const QModelIndex idx = model->index(row, 0);
                const QString text = model->data(idx).toString();
                model->setData(idx, values.contains(text) ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
            }
        } else if (obj->setProperty("selectedValues", values) || obj->setProperty("selectedItems", values)) {
            // handled by custom controls
        } else {
            asError(QStringLiteral("dropdown_multi_select not supported"), error);
            return {};
        }
    } else if (op == QStringLiteral("close_page")) {
        if (!closeObject(obj)) {
            asError(QStringLiteral("close_page not supported"), error);
            return {};
        }
    } else {
        asError(QStringLiteral("unsupported action: %1").arg(action), error);
        return {};
    }

    QCoreApplication::processEvents();
    return {};
}

QJsonValue QtGenericUiAutomationHandler::readProperty(
    const QJsonObject &target,
    const QString &propertyName,
    QString *error) {
    QObject *obj = findTarget(target, error);
    if (!obj) {
        return {};
    }
    const QVariant v = obj->property(propertyName.toUtf8().constData());
    if (!v.isValid()) {
        asError(QStringLiteral("property not found: %1").arg(propertyName), error);
        return {};
    }
    return toJson(v);
}

QJsonValue QtGenericUiAutomationHandler::screenshot(const QString &path, QString *error) {
    QObject *root = rootRequired(error);
    if (!root) {
        return {};
    }
    const QFileInfo info(path);
    info.absoluteDir().mkpath(QStringLiteral("."));

    bool ok = false;
    if (auto *widget = qobject_cast<QWidget *>(root)) {
        ok = widget->grab().save(path);
    } else if (auto *window = qobject_cast<QQuickWindow *>(root)) {
        ok = window->grabWindow().save(path);
    } else if (auto *item = qobject_cast<QQuickItem *>(root)) {
        auto *window = item->window();
        if (window) {
            ok = window->grabWindow().save(path);
        }
    } else if (auto *widget = root->findChild<QWidget *>()) {
        ok = widget->grab().save(path);
    } else if (auto *window = root->findChild<QQuickWindow *>()) {
        ok = window->grabWindow().save(path);
    }

    if (!ok) {
        asError(QStringLiteral("failed to capture screenshot"), error);
        return {};
    }
    return info.absoluteFilePath();
}

QJsonValue QtGenericUiAutomationHandler::dumpTree(QString *error) {
    QObject *root = rootRequired(error);
    if (!root) {
        return {};
    }
    QJsonArray arr;
    const auto objs = root->findChildren<QObject *>();
    for (QObject *obj : objs) {
        QJsonObject line;
        line.insert(QStringLiteral("objectName"), obj->objectName());
        line.insert(QStringLiteral("className"), QString::fromUtf8(obj->metaObject()->className()));
        const QVariant text = obj->property("text");
        if (text.isValid()) {
            line.insert(QStringLiteral("text"), toJson(text));
        }
        arr.push_back(line);
    }
    return arr;
}

QObject *QtGenericUiAutomationHandler::findTarget(const QJsonObject &target, QString *error) const {
    QObject *root = rootRequired(error);
    if (!root) {
        return nullptr;
    }
    const QString kind = target.value(QStringLiteral("kind")).toString().trimmed().toLower();
    const QString value = target.value(QStringLiteral("value")).toString().trimmed();
    if (value.isEmpty()) {
        asError(QStringLiteral("target value is empty"), error);
        return nullptr;
    }

    if (kind == QStringLiteral("objectname") || kind == QStringLiteral("id")) {
        QObject *obj = findByObjectNameLike(value);
        if (!obj) {
            asError(QStringLiteral("target not found by objectName: %1").arg(value), error);
        }
        return obj;
    }
    if (kind == QStringLiteral("path")) {
        const QString last = value.split(QStringLiteral(".")).constLast();
        QObject *obj = findByObjectNameLike(last);
        if (!obj) {
            asError(QStringLiteral("target not found by path: %1").arg(value), error);
        }
        return obj;
    }
    if (kind == QStringLiteral("text") || kind == QStringLiteral("title")) {
        QObject *obj = findByTextLike(value);
        if (!obj) {
            asError(QStringLiteral("target not found by text/title: %1").arg(value), error);
        }
        return obj;
    }
    asError(QStringLiteral("unsupported target kind: %1").arg(kind), error);
    return nullptr;
}

QObject *QtGenericUiAutomationHandler::findByObjectNameLike(const QString &value) const {
    QObject *root = m_root;
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == value) {
        return root;
    }
    if (auto *exact = root->findChild<QObject *>(value, Qt::FindChildrenRecursively)) {
        return exact;
    }
    const auto objs = root->findChildren<QObject *>();
    for (QObject *obj : objs) {
        const QString name = obj->objectName();
        if (name == value || name.endsWith(QStringLiteral(".") + value)) {
            return obj;
        }
    }
    return nullptr;
}

QObject *QtGenericUiAutomationHandler::findByTextLike(const QString &value) const {
    QObject *root = m_root;
    if (!root) {
        return nullptr;
    }
    const auto objs = root->findChildren<QObject *>();
    for (QObject *obj : objs) {
        const QVariant text = obj->property("text");
        if (text.isValid() && text.toString() == value) {
            return obj;
        }
        const QVariant title = obj->property("title");
        if (title.isValid() && title.toString() == value) {
            return obj;
        }
        const QVariant windowTitle = obj->property("windowTitle");
        if (windowTitle.isValid() && windowTitle.toString() == value) {
            return obj;
        }
    }
    return nullptr;
}

bool QtGenericUiAutomationHandler::clickObject(QObject *obj) const {
    if (!obj) {
        return false;
    }
    if (auto *button = qobject_cast<QAbstractButton *>(obj)) {
        button->click();
        return true;
    }
    return invokeNoArgs(obj, "click") || invokeNoArgs(obj, "toggle");
}

bool QtGenericUiAutomationHandler::setChecked(QObject *obj, bool checked) const {
    if (!obj) {
        return false;
    }
    if (auto *button = qobject_cast<QAbstractButton *>(obj)) {
        button->setChecked(checked);
        return true;
    }
    if (obj->setProperty("checked", checked)) {
        return true;
    }
    return invokeSetBool(obj, "setChecked", checked);
}

bool QtGenericUiAutomationHandler::setCurrentText(QObject *obj, const QString &value) const {
    if (!obj) {
        return false;
    }
    if (auto *combo = qobject_cast<QComboBox *>(obj)) {
        const int idx = combo->findText(value);
        if (idx >= 0) {
            combo->setCurrentIndex(idx);
            return true;
        }
        combo->setCurrentText(value);
        return true;
    }
    if (obj->setProperty("currentText", value)) {
        return true;
    }
    return invokeSetString(obj, "setCurrentText", value);
}

bool QtGenericUiAutomationHandler::setCurrentIndex(QObject *obj, int index) const {
    if (!obj) {
        return false;
    }
    if (auto *combo = qobject_cast<QComboBox *>(obj)) {
        combo->setCurrentIndex(index);
        return true;
    }
    if (obj->setProperty("currentIndex", index)) {
        return true;
    }
    return invokeSetInt(obj, "setCurrentIndex", index);
}

bool QtGenericUiAutomationHandler::closeObject(QObject *obj) const {
    if (!obj) {
        return false;
    }
    if (auto *widget = qobject_cast<QWidget *>(obj)) {
        widget->close();
        return true;
    }
    if (auto *window = qobject_cast<QQuickWindow *>(obj)) {
        window->close();
        return true;
    }
    return invokeNoArgs(obj, "close") || obj->setProperty("visible", false);
}

bool QtGenericUiAutomationHandler::setTextValue(QObject *obj, const QString &value) const {
    if (!obj) {
        return false;
    }
    if (auto *lineEdit = qobject_cast<QLineEdit *>(obj)) {
        lineEdit->setText(value);
        return true;
    }
    if (obj->setProperty("text", value)) {
        return true;
    }
    return invokeSetString(obj, "setText", value);
}

QObject *QtGenericUiAutomationHandler::rootRequired(QString *error) const {
    if (m_root) {
        return m_root;
    }
    asError(QStringLiteral("root object is not configured"), error);
    return nullptr;
}

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
