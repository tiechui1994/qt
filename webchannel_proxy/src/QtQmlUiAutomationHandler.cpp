#include "UiAutomationProxyServer.h"
#include "UiQMLQuery.h"

#include <QQmlApplicationEngine>
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QMetaMethod>
#include <QMetaType>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QElapsedTimer>
#include <QVariantList>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

namespace {
QString setError(const QString &message, QString *error) {
    if (error) {
        *error = message;
    }
    return message;
}

QJsonValue toJson(const QVariant &value) {
    return QJsonValue::fromVariant(value);
}

bool invokeNoArgs(QObject *obj, const char *method) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection);
}

bool invokeSetBool(QObject *obj, const char *method, bool value) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection, Q_ARG(bool, value));
}

bool invokeSetInt(QObject *obj, const char *method, int value) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection, Q_ARG(int, value));
}

bool invokeSetString(QObject *obj, const char *method, const QString &value) {
    if (!obj) {
        return false;
    }
    return QMetaObject::invokeMethod(obj, method, Qt::DirectConnection, Q_ARG(QString, value));
}

// Find method by name; prefers 0 args, then 1 arg (QVariant or basic type). Returns invalid if not found.
QMetaMethod findInvokableMethod(QObject *obj, const QByteArray &methodName) {
    if (!obj) {
        return QMetaMethod();
    }
    const QMetaObject *mo = obj->metaObject();
    QMetaMethod candidate;
    int candidateParamCount = -1;

    for (int i = 0; i < mo->methodCount(); ++i) {
        QMetaMethod m = mo->method(i);
        
        // 修正 1: 必须包含 QMetaMethod::Signal
        // 在 QML 中，自定义信号（如 cusClicked）在 C++ 侧被识别为 Signal 类型 
        if (m.methodType() != QMetaMethod::Slot && 
            m.methodType() != QMetaMethod::Method && 
            m.methodType() != QMetaMethod::Signal) {
            continue;
        }

        if (m.name() != methodName) {
            continue;
        }

        const int paramCount = m.parameterCount();
        
        // 优先选择无参方法
        if (paramCount == 0) {
            return m;
        }

        // 修正 2: 处理带 1 个参数的情况（对应 QML 中的 cusClicked(mouse: var)） 
        if (paramCount == 1 && candidateParamCount != 0) {
            const int typeId = m.parameterType(0);
            
            // QML 中的 'var' 类型在 C++ 元对象系统中映射为 QMetaType::QVariant 
            if (typeId == QMetaType::QVariant || typeId == QMetaType::Bool
                || typeId == QMetaType::Int || typeId == QMetaType::QString) {
                
                if (candidateParamCount < 0 || paramCount < candidateParamCount) {
                    candidate = m;
                    candidateParamCount = paramCount;
                }
            }
        }
    }
    return candidateParamCount >= 0 ? candidate : QMetaMethod();
}

// Invoke method by name; determines parameter types and converts args or passes QVariant dummy.
bool invokeWithSignature(QObject *obj, const QByteArray &methodName, const QVariantList &args, QString *error) {
    if (!obj) {
        if (error) *error = QStringLiteral("object is null");
        return false;
    }
    QMetaMethod method = findInvokableMethod(obj, methodName);
    if (!method.isValid()) {
        if (error) *error = QStringLiteral("method not found or not invokable: %1").arg(QString::fromUtf8(methodName));
        return false;
    }
    const int paramCount = method.parameterCount();
    if (paramCount == 0) {
        return QMetaObject::invokeMethod(obj, methodName.constData(), Qt::DirectConnection);
    }
    if (paramCount == 1) {
        const int typeId = method.parameterType(0);
        QVariant arg = (args.size() > 0 && args.at(0).isValid()) ? args.at(0) : QVariant();
        if (typeId == QMetaType::QVariant) {
            return QMetaObject::invokeMethod(obj, methodName.constData(), Qt::DirectConnection, Q_ARG(QVariant, arg));
        }
        if (typeId == QMetaType::Bool) {
            return QMetaObject::invokeMethod(obj, methodName.constData(), Qt::DirectConnection, Q_ARG(bool, arg.toBool()));
        }
        if (typeId == QMetaType::Int) {
            return QMetaObject::invokeMethod(obj, methodName.constData(), Qt::DirectConnection, Q_ARG(int, arg.toInt()));
        }
        if (typeId == QMetaType::QString) {
            return QMetaObject::invokeMethod(obj, methodName.constData(), Qt::DirectConnection, Q_ARG(QString, arg.toString()));
        }
        if (error) *error = QStringLiteral("unsupported parameter type for method: %1").arg(QString::fromUtf8(methodName));
        return false;
    }
    if (error) *error = QStringLiteral("method has more than one parameter: %1").arg(QString::fromUtf8(methodName));
    return false;
}

int findModelIndexForText(const QVariant &model, const QString &text) {
    if (!model.isValid()) {
        return -1;
    }

    const QStringList stringList = model.toStringList();
    if (!stringList.isEmpty()) {
        return stringList.indexOf(text);
    }

    const QVariantList variantList = model.toList();
    for (int i = 0; i < variantList.size(); ++i) {
        if (variantList.at(i).toString() == text) {
            return i;
        }
    }

    QObject *objModel = model.value<QObject *>();
    auto *itemModel = qobject_cast<QAbstractItemModel *>(objModel);
    if (!itemModel) {
        return -1;
    }
    for (int row = 0; row < itemModel->rowCount(); ++row) {
        const QModelIndex idx = itemModel->index(row, 0);
        if (itemModel->data(idx).toString() == text) {
            return row;
        }
    }
    return -1;
}

// 查找超时与轮询间隔（每 100ms 执行一次 processEvents 后遍历所有根节点）
static const int kFindTimeoutMs = 500;
static const int kFindIntervalMs = 100;
}  // namespace

QtQmlUiAutomationHandler::QtQmlUiAutomationHandler(QQmlApplicationEngine *engine)
    : m_engine(engine) {}

void QtQmlUiAutomationHandler::setEngine(QQmlApplicationEngine *engine) {
    m_engine = engine;
}

QQmlApplicationEngine *QtQmlUiAutomationHandler::engine() const {
    return m_engine;
}

QObject *QtQmlUiAutomationHandler::getRoot() const {
    if (!m_engine || m_engine->rootObjects().isEmpty()) {
        return nullptr;
    }
    return m_engine->rootObjects().constFirst();
}

QJsonValue QtQmlUiAutomationHandler::resolve(const QJsonObject &target, QString *error) {
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

QJsonValue QtQmlUiAutomationHandler::executeAction(
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
    if (op == QStringLiteral("close_page") && target.isEmpty()) {
        obj = root;
    } else {
        obj = findTarget(target, error);
        if (!obj) {
            return {};
        }
    }

    if (op == QStringLiteral("click")) {
        QString methodName;
        QVariantList args;
        if (!value.isUndefined() && !value.isNull()) {
            if (value.isString()) {
                methodName = value.toString().trimmed();
            } else if (value.isObject()) {
                const QJsonObject vo = value.toObject();
                methodName = vo.value(QStringLiteral("method")).toString().trimmed();
                if (vo.contains(QStringLiteral("args"))) {
                    const QJsonValue av = vo.value(QStringLiteral("args"));
                    if (av.isArray()) {
                        args = av.toArray().toVariantList();
                    }
                }
            }
        }
        QString err;
        if (!clickObject(obj, methodName, &err, args)) {
            setError(QStringLiteral("click not supported by target: %1").arg(err), error);
            return {};
        }
    } else if (op == QStringLiteral("input")) {
        if (!setPropertyValue(obj, QStringLiteral("text"), value.toString())) {
            setError(QStringLiteral("input requires a text-capable target"), error);
            return {};
        }
    } else if (op == QStringLiteral("upload")) {
        const QString abs = QFileInfo(value.toString()).absoluteFilePath();
        if (!setPropertyValue(obj, QStringLiteral("text"), abs)) {
            setError(QStringLiteral("upload requires a text-capable target"), error);
            return {};
        }
    } else if (op == QStringLiteral("select")) {
        if (!setPropertyValue(obj, QStringLiteral("currentText"), value.toString())) {
            setError(QStringLiteral("select requires currentText/currentIndex support"), error);
            return {};
        }
    } else if (op == QStringLiteral("switch_page")) {
        if (value.isDouble()) {
            if (!setPropertyValue(obj, QStringLiteral("currentIndex"), value.toInt())) {
                setError(QStringLiteral("switch_page(index) not supported"), error);
                return {};
            }
        } else {
            bool asInt = false;
            const QString raw = value.toString();
            const int idx = raw.toInt(&asInt);
            if (asInt) {
                if (!setPropertyValue(obj, QStringLiteral("currentIndex"), idx)) {
                    setError(QStringLiteral("switch_page(index) not supported"), error);
                    return {};
                }
            } else if (!setPropertyValue(obj, QStringLiteral("currentText"), raw)) {
                setError(QStringLiteral("switch_page(text) not supported"), error);
                return {};
            }
        }
    } else if (op == QStringLiteral("slide")) {
        if (!obj->setProperty("value", value.toVariant()) &&
            !invokeSetInt(obj, "setValue", value.toInt())) {
            setError(QStringLiteral("slide requires value support"), error);
            return {};
        }
    } else if (op == QStringLiteral("toggle")) {
        if (value.isBool()) {
            if (!setPropertyValue(obj, QStringLiteral("checked"), value.toBool())) {
                setError(QStringLiteral("toggle(bool) not supported"), error);
                return {};
            }
        } else if (!clickObject(obj)) {
            setError(QStringLiteral("toggle requires click or checked support"), error);
            return {};
        }
    } else if (op == QStringLiteral("single_select")) {
        if (!setPropertyValue(obj, QStringLiteral("checked"), true) && !clickObject(obj)) {
            setError(QStringLiteral("single_select not supported"), error);
            return {};
        }
    } else if (op == QStringLiteral("close_page")) {
        if (!closeObject(obj)) {
            setError(QStringLiteral("close_page not supported"), error);
            return {};
        }
    } else if ( op == QStringLiteral("set")) {
        const QJsonObject vo = value.toObject();
        const QString key = vo.value(QStringLiteral("property")).toString();
        const QVariant val = vo.value(QStringLiteral("value")).toVariant();
        if (!setPropertyValue(obj, key, val)) {
            setError(QStringLiteral("set requires a property support"), error);
            return {};
        }
    } else {
        setError(QStringLiteral("unsupported action: %1").arg(action), error);
        return {};
    }

    QCoreApplication::processEvents();
    return {};
}

QJsonValue QtQmlUiAutomationHandler::readProperty(
    const QJsonObject &target,
    const QString &propertyName,
    QString *error) {
    QObject *obj = findTarget(target, error);
    if (!obj) {
        return {};
    }
    const QVariant value = obj->property(propertyName.toUtf8().constData());
    if (!value.isValid()) {
        setError(QStringLiteral("property not found: %1").arg(propertyName), error);
        return {};
    }
    return toJson(value);
}

QJsonValue QtQmlUiAutomationHandler::screenshot(const QString &path, QString *error) {
    QObject *root = rootRequired(error);
    if (!root) {
        return {};
    }
    const QFileInfo info(path);
    info.absoluteDir().mkpath(QStringLiteral("."));

    bool ok = false;
    if (auto *window = qobject_cast<QQuickWindow *>(root)) {
        ok = window->grabWindow().save(path);
    } else if (auto *item = qobject_cast<QQuickItem *>(root)) {
        if (auto *window = item->window()) {
            ok = window->grabWindow().save(path);
        }
    } else if (auto *window = root->findChild<QQuickWindow *>()) {
        ok = window->grabWindow().save(path);
    } else if (auto *item = root->findChild<QQuickItem *>()) {
        if (auto *window = item->window()) {
            ok = window->grabWindow().save(path);
        }
    }

    if (!ok) {
        setError(QStringLiteral("failed to capture screenshot"), error);
        return {};
    }
    return info.absoluteFilePath();
}

QJsonValue QtQmlUiAutomationHandler::dumpTree(QString *error) {
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
        const QVariant idValue = obj->property("id");
        if (idValue.isValid()) {
            line.insert(QStringLiteral("id"), toJson(idValue));
        }
        const QVariant text = obj->property("text");
        if (text.isValid()) {
            line.insert(QStringLiteral("text"), toJson(text));
        }
        arr.push_back(line);
    }
    return arr;
}

QObject *QtQmlUiAutomationHandler::findTarget(const QJsonObject &target, QString *error) const {
    const int timeout = target.value(QStringLiteral("timeout")).toInt(0);
    if (timeout <= 0) {
        return findTargetOnce(target, error);
    }

    QString lastError;
    QString *iterError = error ? &lastError : nullptr;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
        const qint64 iterStart = timer.elapsed();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QObject *obj = findTargetOnce(target, iterError);
        if (obj) {
            if (error) {
                error->clear();
            }
            return obj;
        }
        if (timer.elapsed() >= timeout) {
            break;
        }
        const int toSleep = kFindIntervalMs - static_cast<int>(timer.elapsed() - iterStart);
        if (toSleep > 0) {
            QThread::msleep(static_cast<unsigned long>(toSleep));
        }
    }
    if (error) {
        *error = lastError;
    }
    return nullptr;
}

QObject *QtQmlUiAutomationHandler::findTargetOnce(const QJsonObject &target, QString *error) const {
    const QList<QObject *> roots = m_engine ? m_engine->rootObjects() : QList<QObject *>();
    if (roots.isEmpty()) {
        setError(QStringLiteral("root object is not configured (engine is null or has no root objects)"), error);
        return nullptr;
    }

    const QString kind = target.value(QStringLiteral("kind")).toString().trimmed().toLower();
    const QString value = target.value(QStringLiteral("value")).toString().trimmed();
    const bool debug = target.value(QStringLiteral("debug")).toBool();
    if (value.isEmpty()) {
        setError(QStringLiteral("target value is empty"), error);
        return nullptr;
    }

    if (kind == QStringLiteral("selector")) {
        QString err;
        for (QObject *root : roots) {
            QmlQuerySelector selector;
            QObject *obj = selector.querySelector(root, value, &err, debug);
            if (obj) {
                if (error) {
                    error->clear();
                }
                return obj;
            }
        }
        setError(QStringLiteral("target not found by selector %1: %2").arg(value).arg(err), error);
        return nullptr;
    }

    const auto findByObjectNameLikeOnce = [&](const QString &value) -> QObject * {
        for (QObject *root : roots) {
            if (!root) {
                continue;
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
        }
        return nullptr;
    };

    const auto findByTextLikeOnce = [&](const QString &value) -> QObject * {
        for (QObject *root : roots) {
            if (!root) {
                continue;
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
                const QVariant placeholder = obj->property("placeholderText");
                if (placeholder.isValid() && placeholder.toString() == value) {
                    return obj;
                }
            }
            if (root->property("text").toString() == value
                || root->property("title").toString() == value
                || root->property("placeholderText").toString() == value) {
                return root;
            }
        }
        return nullptr;
    };

    if (kind == QStringLiteral("objectname")) {
        QObject *obj = findByObjectNameLikeOnce(value);
        if (!obj) {
            setError(QStringLiteral("target not found by objectName: %1").arg(value), error);
        } else if (error) {
            error->clear();
        }
        return obj;
    }
    if (kind == QStringLiteral("path")) {
        const QString last = value.split(QStringLiteral(".")).constLast();
        QObject *obj = findByObjectNameLikeOnce(last);
        if (!obj) {
            setError(QStringLiteral("target not found by path: %1").arg(value), error);
        } else if (error) {
            error->clear();
        }
        return obj;
    }
    if (kind == QStringLiteral("text") || kind == QStringLiteral("title")) {
        QObject *obj = findByTextLikeOnce(value);
        if (!obj) {
            setError(QStringLiteral("target not found by text/title: %1").arg(value), error);
        } else if (error) {
            error->clear();
        }
        return obj;
    }

    setError(QStringLiteral("unsupported target kind: %1").arg(kind), error);
    return nullptr;
}

bool QtQmlUiAutomationHandler::clickObject(QObject *obj, const QString &methodName, QString *error, const QVariantList &args) const {
    if (!obj) {
        return false;
    }
    if (!methodName.isEmpty()) {
        return invokeWithSignature(obj, methodName.toUtf8(), args, error);
    }
    return  invokeNoArgs(obj, "click")
        || invokeNoArgs(obj, "clicked")
        || invokeNoArgs(obj, "toggle")
        || invokeNoArgs(obj, "trigger");
}

bool QtQmlUiAutomationHandler::closeObject(QObject *obj) const {
    if (!obj) {
        return false;
    }
    return invokeNoArgs(obj, "close") || setPropertyValue(obj, QStringLiteral("visible"), false);
}

bool QtQmlUiAutomationHandler::setPropertyValue(QObject *obj, const QString &property, const QVariant &value) const {
    if (!obj) {
        return false;
    }
    if (obj->setProperty(property.toUtf8().constData(), value)) {
        return true;
    }

    QString methodName;
    QVariantList args;
    if (property == QStringLiteral("text")) {
        methodName = "setText";
        args << value;
    } else if (property == QStringLiteral("checked")) {
        methodName = "setChecked";
        args << value.toBool();
    } else if (property == QStringLiteral("currentIndex")) {
        methodName = "setCurrentIndex";
        args << value.toInt();
    } else if (property == QStringLiteral("visible")) {
        methodName = "setVisible";
        args << value.toBool();
    } else if (property == QStringLiteral("enabled")) {
        methodName = "setEnabled";
        args << value.toBool();
    } else if (property == QStringLiteral("focus")) {
        methodName = "setFocus";
        args << value.toBool();
    }

    return invokeWithSignature(obj, methodName.toUtf8(), args, nullptr) || 
           invokeWithSignature(obj, "setProperty", QVariantList() << property << value, nullptr);
}

QObject *QtQmlUiAutomationHandler::rootRequired(QString *error) const {
    QObject *root = getRoot();
    if (root) {
        return root;
    }
    setError(QStringLiteral("root object is not configured (engine is null or has no root objects)"), error);
    return nullptr;
}
