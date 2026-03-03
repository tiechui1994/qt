#include <windows.h>
#include <detours.h>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtCore/QDebug>
#include <QtCore/QUrl>
#include <QtCore/QObject>
#include <QtCore/QVariant>
#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickWindow>
#include <atomic>
#include <QFile>
#include "UiAutomationProxyServer.h"

// --- 1. 自动化代理对象 ---
class AutomationProxy : public QObject {
    Q_OBJECT
public:
    explicit AutomationProxy(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QString getStatus() { return "HOOK_SUCCESS"; }
};

typedef void (*LoadUrlFn)(QQmlApplicationEngine*, const QUrl&);
static LoadUrlFn TrueLoadUrl = nullptr;
static std::atomic<bool> gShownHookDialog{false};
static UiAutomationProxyServer* gProxyServer = nullptr;

// 转换工具
template<typename T, typename U>
void* GetMemberAddr(U T::* func) {
    union { U T::* f; void* p; } u;
    u.f = func;
    return u.p;
}

static void EnsureProxyServer(QObject* rootObject) {
    if (!rootObject) {
        return;
    }

    if (!gProxyServer) {
        QObject* app = QCoreApplication::instance();
        gProxyServer = app ? new UiAutomationProxyServer(app) : new UiAutomationProxyServer();
    }

    // Keep handler root aligned with the latest loaded root object.
    gProxyServer->useDefaultQmlHandler(rootObject);

    if (!gProxyServer->isListening()) {
        if (!gProxyServer->start(12345, QHostAddress::Any, QStringLiteral("demo-token"))) {
            qWarning() << "[QtHook] Failed to listen on 0.0.0.0:12345";
        } else {
            qInfo() << "[QtHook] UiAutomationProxyServer listening on" << gProxyServer->serverPort();
        }
    }
}

// --- 核心：Hook 后的增强行为 ---
void HookedLoadUrl(QQmlApplicationEngine* _this, const QUrl& url) {
    // 只提示一次，避免每次 load 都弹窗阻塞 UI。
    bool expected = false;
    if (gShownHookDialog.compare_exchange_strong(expected, true)) {
        if (QFile::exists(QString("flag.txt"))) {
            MessageBoxA(
                NULL,
                "Detours Hook Success!\nQQmlApplicationEngine::load() intercepted.",
                "Qt Auto Agent",
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST
            );
        }
    }

    // 执行原始加载逻辑
    if (TrueLoadUrl) {
        TrueLoadUrl(_this, url);
    }

    // 【证明 3】注入代理并尝试修改窗口标题
    QQmlContext* context = _this->rootContext();
    if (context) {
        if (!context->property("_qt_auto_agent_injected").toBool()) {
            context->setContextProperty("AutomationProxy", new AutomationProxy(_this));
            context->setProperty("_qt_auto_agent_injected", true);
        }
        
        // 延迟一小会儿获取根对象，修改窗口标题
        auto rootObjects = _this->rootObjects();
        if (!rootObjects.isEmpty()) {
            QObject* firstObj = rootObjects.first();
            EnsureProxyServer(firstObj);

            QQuickWindow* window = qobject_cast<QQuickWindow*>(firstObj);
            if (window) {
                window->setTitle("HOOKED - " + window->title());
            }
        }
    }
}

// 必须导出至少一个函数
extern "C" __declspec(dllexport) void __stdcall QtAutoAgentExport() {}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved) {
    if (DetourIsHelperProcess()) return TRUE;

    if (dwReason == DLL_PROCESS_ATTACH) {
        DetourRestoreAfterWith();

        // 记录一下 DLL 加载
        OutputDebugStringA("[QtHook] DLL Attached to Process\n");

        auto loadUrlMethod = static_cast<void (QQmlApplicationEngine::*)(const QUrl&)>(&QQmlApplicationEngine::load);
        TrueLoadUrl = reinterpret_cast<LoadUrlFn>(GetMemberAddr(loadUrlMethod));
        void* pDetour = (void*)HookedLoadUrl;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)TrueLoadUrl, pDetour);
        
        if (DetourTransactionCommit() == NO_ERROR) {
            OutputDebugStringA("[QtHook] Hook Transaction Committed\n");
        }
    } 
    return TRUE;
}

#include "main.moc"