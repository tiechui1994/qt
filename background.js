// background.js

let currentTabId = null; // 存储当前操作的 tabId

chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
    // 接收来自 content.js 的元素信息
    if (request.type === "elementSelected") {
        console.log("elementSelected:", request.info);
        // 可以将此信息存储起来，或者发送给 popup.html
        chrome.runtime.sendMessage({
            type: "displayElementInfo",
            info: request.info
        }).then(response => {

        }).catch(error => {
            console.error("发送消息到内容脚本失败:", error);
            sendResponse({ status: "failed", error: error.message });
        });
    }

    // 接收来自 popup.html 的启动/停止选择模式请求
    if (request.type === "startPickingMode") {
        console.log("startPickingMode:", request, sender);
        currentTabId = request.tab.id; // 记录发起请求的 tabId
        // send to content.js "startPicking"
        chrome.tabs.sendMessage(currentTabId, { type: "startPicking" })
            .then(response => {
                console.log("content.js response:", response);
                sendResponse({ status: "pickingStarted" });
            })
            .catch(error => {
                console.error("send to content.js failed:", error);
                sendResponse({ status: "failed", error: error.message });
            });
        return true; // 表示异步发送响应
    } else if (request.type === "stopPickingMode") {
        if (currentTabId) {
            // send to content.js "pickingStopped"
            chrome.tabs.sendMessage(currentTabId, { type: "stopPicking" })
                .then(response => {
                    console.log("content.js response:", response);
                    sendResponse({ status: "pickingStopped" });
                    currentTabId = null; // 清除 tabId
                })
                .catch(error => {
                    console.error("send to content.js failed:", error);
                    sendResponse({ status: "failed", error: error.message });
                });
            return true; // 表示异步发送响应
        } else {
            sendResponse({ status: "noPickingActive" });
        }
    }
});

