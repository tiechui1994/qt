#include "UiAutomationProxyServer.h"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantList>

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
}  // namespace

QtQmlUiAutomationHandler::QtQmlUiAutomationHandler(QObject *rootObject)
    : m_root(rootObject) {}

void QtQmlUiAutomationHandler::setRootObject(QObject *rootObject) {
    m_root = rootObject;
}

QObject *QtQmlUiAutomationHandler::rootObject() const {
    return m_root;
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
            setError(QStringLiteral("click not supported by target"), error);
            return {};
        }
    } else if (op == QStringLiteral("input")) {
        if (!setTextValue(obj, value.toString())) {
            setError(QStringLiteral("input requires a text-capable target"), error);
            return {};
        }
    } else if (op == QStringLiteral("upload")) {
        const QString abs = QFileInfo(value.toString()).absoluteFilePath();
        if (!setTextValue(obj, abs)) {
            setError(QStringLiteral("upload requires a text-capable target"), error);
            return {};
        }
    } else if (op == QStringLiteral("select")) {
        if (!setCurrentText(obj, value.toString())) {
            setError(QStringLiteral("select requires currentText/currentIndex support"), error);
            return {};
        }
    } else if (op == QStringLiteral("switch_page")) {
        if (value.isDouble()) {
            if (!setCurrentIndex(obj, value.toInt())) {
                setError(QStringLiteral("switch_page(index) not supported"), error);
                return {};
            }
        } else {
            bool asInt = false;
            const QString raw = value.toString();
            const int idx = raw.toInt(&asInt);
            if (asInt) {
                if (!setCurrentIndex(obj, idx)) {
                    setError(QStringLiteral("switch_page(index) not supported"), error);
                    return {};
                }
            } else if (!setCurrentText(obj, raw)) {
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
            if (!setChecked(obj, value.toBool())) {
                setError(QStringLiteral("toggle(bool) not supported"), error);
                return {};
            }
        } else if (!clickObject(obj)) {
            setError(QStringLiteral("toggle requires click or checked support"), error);
            return {};
        }
    } else if (op == QStringLiteral("single_select")) {
        if (!setChecked(obj, true) && !clickObject(obj)) {
            setError(QStringLiteral("single_select not supported"), error);
            return {};
        }
    } else if (op == QStringLiteral("multi_select")) {
        QVariantList values;
        if (value.isArray()) {
            values = value.toArray().toVariantList();
        } else {
            values.push_back(value.toVariant());
        }
        for (const QVariant &entry : values) {
            const QString token = entry.toString();
            QObject *item = findByObjectNameLike(token);
            if (!item) {
                item = findByTextLike(token);
            }
            if (!item) {
                setError(QStringLiteral("multi_select option not found: %1").arg(token), error);
                return {};
            }
            if (!setChecked(item, true) && !clickObject(item)) {
                setError(QStringLiteral("multi_select option not checkable: %1").arg(token), error);
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
        if (!(obj->setProperty("selectedValues", values) || obj->setProperty("selectedItems", values))) {
            setError(QStringLiteral("dropdown_multi_select not supported"), error);
            return {};
        }
    } else if (op == QStringLiteral("close_page")) {
        if (!closeObject(obj)) {
            setError(QStringLiteral("close_page not supported"), error);
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
    QObject *root = rootRequired(error);
    if (!root) {
        return nullptr;
    }

    const QString kind = target.value(QStringLiteral("kind")).toString().trimmed().toLower();
    const QString value = target.value(QStringLiteral("value")).toString().trimmed();
    if (value.isEmpty()) {
        setError(QStringLiteral("target value is empty"), error);
        return nullptr;
    }

    if (kind == QStringLiteral("objectname")) {
        QObject *obj = findByObjectNameLike(value);
        if (!obj) {
            setError(QStringLiteral("target not found by objectName: %1").arg(value), error);
        }
        return obj;
    }
    if (kind == QStringLiteral("path")) {
        const QString last = value.split(QStringLiteral(".")).constLast();
        QObject *obj = findByObjectNameLike(last);
        if (!obj) {
            setError(QStringLiteral("target not found by path: %1").arg(value), error);
        }
        return obj;
    }
    if (kind == QStringLiteral("text") || kind == QStringLiteral("title")) {
        QObject *obj = findByTextLike(value);
        if (!obj) {
            setError(QStringLiteral("target not found by text/title: %1").arg(value), error);
        }
        return obj;
    }

    setError(QStringLiteral("unsupported target kind: %1").arg(kind), error);
    return nullptr;
}

QObject *QtQmlUiAutomationHandler::findByObjectNameLike(const QString &value) const {
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

QObject *QtQmlUiAutomationHandler::findByTextLike(const QString &value) const {
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
        const QVariant placeholder = obj->property("placeholderText");
        if (placeholder.isValid() && placeholder.toString() == value) {
            return obj;
        }
    }
    return nullptr;
}

bool QtQmlUiAutomationHandler::clickObject(QObject *obj) const {
    if (!obj) {
        return false;
    }
    return invokeNoArgs(obj, "click")
        || invokeNoArgs(obj, "clicked")
        || invokeNoArgs(obj, "toggle")
        || invokeNoArgs(obj, "trigger");
}

bool QtQmlUiAutomationHandler::setChecked(QObject *obj, bool checked) const {
    if (!obj) {
        return false;
    }
    if (obj->setProperty("checked", checked)) {
        return true;
    }
    return invokeSetBool(obj, "setChecked", checked);
}

bool QtQmlUiAutomationHandler::setCurrentText(QObject *obj, const QString &value) const {
    if (!obj) {
        return false;
    }
    if (obj->setProperty("currentText", value)) {
        return true;
    }
    const int idx = findModelIndexForText(obj->property("model"), value);
    if (idx >= 0 && setCurrentIndex(obj, idx)) {
        return true;
    }
    return invokeSetString(obj, "setCurrentText", value);
}

bool QtQmlUiAutomationHandler::setCurrentIndex(QObject *obj, int index) const {
    if (!obj) {
        return false;
    }
    if (obj->setProperty("currentIndex", index)) {
        return true;
    }
    return invokeSetInt(obj, "setCurrentIndex", index);
}

bool QtQmlUiAutomationHandler::closeObject(QObject *obj) const {
    if (!obj) {
        return false;
    }
    return invokeNoArgs(obj, "close") || obj->setProperty("visible", false);
}

bool QtQmlUiAutomationHandler::setTextValue(QObject *obj, const QString &value) const {
    if (!obj) {
        return false;
    }
    if (obj->setProperty("text", value)) {
        return true;
    }
    return invokeSetString(obj, "setText", value);
}

QObject *QtQmlUiAutomationHandler::rootRequired(QString *error) const {
    if (m_root) {
        return m_root;
    }
    setError(QStringLiteral("root object is not configured"), error);
    return nullptr;
}

#include "QtQmlUiAutomationHandler.moc"
