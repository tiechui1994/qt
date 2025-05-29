// popup.js

const startPickingBtn = document.getElementById('startPickingBtn');
const stopPickingBtn = document.getElementById('stopPickingBtn');
const elementInfoDiv = document.getElementById('elementInfo');

let isPickingModeActive = false; // popup 自身的状态

// 处理来自背景脚本的消息 (content.js 发送过来的元素信息)
chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
    if (request.type === "displayElementInfo") {
        displayElementInfo(request.info);
    }
});

startPickingBtn.addEventListener('click', async () => {
    if (isPickingModeActive) return;

    // 获取当前活动标签页
    let [tab] = await chrome.tabs.query({ active: true, currentWindow: true });

    if (tab) {
        // 向 background.js 发送启动选择模式的消息，背景脚本会转发给 content.js
        chrome.runtime.sendMessage({ type: "startPickingMode", tab: tab })
            .then(response => {
                if (response && response.status === "pickingStarted") {
                    isPickingModeActive = true;
                    startPickingBtn.style.display = 'none';
                    stopPickingBtn.style.display = 'block';
                    elementInfoDiv.innerHTML = '<p>请在网页上点击一个元素...</p>';
                } else {
                    elementInfoDiv.innerHTML = '<p style="color: red;">启动选择模式失败。</p>';
                }
            })
            .catch(error => {
                console.error("发送启动消息失败:", error);
                elementInfoDiv.innerHTML = `<p style="color: red;">通信错误: ${error.message}</p>`;
            });
    }
});

stopPickingBtn.addEventListener('click', async () => {
    if (!isPickingModeActive) return;

    let [tab] = await chrome.tabs.query({ active: true, currentWindow: true });

    if (tab) {
        chrome.runtime.sendMessage({ type: "stopPickingMode", tab: tab })
            .then(response => {
                if (response && response.status === "pickingStopped") {
                    isPickingModeActive = false;
                    startPickingBtn.style.display = 'block';
                    stopPickingBtn.style.display = 'none';
                    elementInfoDiv.innerHTML = '<p>选择模式已停止。</p>';
                } else {
                    elementInfoDiv.innerHTML = '<p style="color: red;">停止选择模式失败。</p>';
                }
            })
            .catch(error => {
                console.error("发送停止消息失败:", error);
                elementInfoDiv.innerHTML = `<p style="color: red;">通信错误: ${error.message}</p>`;
            });
    }
});

// 在弹出界面显示元素信息
function displayElementInfo(info) {
    if (!info) {
        elementInfoDiv.innerHTML = '<p>未能获取元素信息。</p>';
        return;
    }

    elementInfoDiv.innerHTML = `
        <p><strong>标签:</strong> &lt;${info.tagName}&gt;</p>
        ${info.id ? `<p><strong>ID:</strong> ${info.id}</p>` : ''}
        ${info.classes.length > 0 ? `<p><strong>类:</strong> ${info.classes.join(', ')}</p>` : ''}
        <p><strong>尺寸:</strong> ${Math.round(info.position.width)}px x ${Math.round(info.position.height)}px</p>
        <p><strong>位置 (视口):</strong> X:${Math.round(info.position.left)} Y:${Math.round(info.position.top)}</p>
        <p><strong>位置 (文档):</strong> X:${Math.round(info.position.absoluteLeft)} Y:${Math.round(info.position.absoluteTop)}</p>
        <p><strong>文本内容 (截断):</strong> <pre>${info.textContent}</pre></p>
        <p><strong>部分HTML:</strong> <pre>${escapeHtml(info.outerHTML)}</pre></p>
        <p><strong>计算样式 (部分):</strong></p>
        <ul>
            <li>Display: ${info.computedStyle.display}</li>
            <li>Position: ${info.computedStyle.position}</li>
            <li>Font Size: ${info.computedStyle.fontSize}</li>
            <li>Background: ${info.computedStyle.backgroundColor}</li>
            <li>Color: ${info.computedStyle.color}</li>
        </ul>
    `;
}

// 简单的 HTML 转义函数，防止 XSS
function escapeHtml(text) {
    const map = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#039;'
    };
    return text.replace(/[&<>"']/g, function(m) { return map[m]; });
}
