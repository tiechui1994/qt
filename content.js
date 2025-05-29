// content.js

let currentHighlightedElement = null;
let currentSelectedElement = null;
let isPickingMode = false; // 插件是否处于元素选择模式

// 创建一个用于显示元素信息的浮动面板 (可选，可以在这里显示，也可以只发送给 popup)
const infoOverlay = document.createElement('div');
infoOverlay.style.position = 'fixed';
infoOverlay.style.background = 'rgba(0, 0, 0, 0.7)';
infoOverlay.style.color = 'white';
infoOverlay.style.padding = '5px 10px';
infoOverlay.style.fontSize = '12px';
infoOverlay.style.zIndex = '999999';
infoOverlay.style.pointerEvents = 'none'; // 确保不干扰鼠标事件
infoOverlay.style.borderRadius = '3px';
infoOverlay.style.display = 'none'; // 默认隐藏
document.body.appendChild(infoOverlay);


// 鼠标移动事件：高亮元素
function handleMouseMove(event) {
    if (!isPickingMode) return;

    // 避免高亮自身插件元素
    if (event.target === infoOverlay || infoOverlay.contains(event.target)) {
        removeHighlight();
        return;
    }

    if (currentHighlightedElement && currentHighlightedElement !== event.target) {
        removeHighlight();
    }

    if (event.target && event.target !== currentHighlightedElement) {
        currentHighlightedElement = event.target;
        currentHighlightedElement.classList.add('plugin-highlight-element');
        updateInfoOverlay(currentHighlightedElement, event.clientX, event.clientY);
    }
}

// 鼠标点击事件：选中元素并发送信息
function handleClick(event) {
    if (!isPickingMode) return;

    event.preventDefault(); // 阻止默认点击行为，如链接跳转
    event.stopPropagation(); // 阻止事件冒泡

    if (currentSelectedElement) {
        currentSelectedElement.classList.remove('plugin-highlight-element-selected');
    }

    currentSelectedElement = event.target;
    currentSelectedElement.classList.add('plugin-highlight-element-selected');

    const elementInfo = getElementDetails(currentSelectedElement);
    console.log("选中元素详情:", elementInfo);

    // 发送信息给背景脚本
    chrome.runtime.sendMessage({
        type: "elementSelected",
        info: elementInfo
    }).then(() => {
        console.info("send to background.js elementSelected")
    });

    // 选中后退出选择模式
    //exitPickingMode();
}

// 退出高亮模式
function removeHighlight() {
    if (currentHighlightedElement) {
        currentHighlightedElement.classList.remove('plugin-highlight-element');
        currentHighlightedElement = null;
    }
    infoOverlay.style.display = 'none';
}

// 获取元素的详细信息
function getElementDetails(element) {
    if (!element) return null;

    const rect = element.getBoundingClientRect();
    const computedStyle = window.getComputedStyle(element);

    return {
        tagName: element.tagName.toLowerCase(),
        id: element.id || null,
        classes: Array.from(element.classList),
        attributes: Array.from(element.attributes).map(attr => ({ name: attr.name, value: attr.value })),
        textContent: element.textContent ? element.textContent.trim().substring(0, 200) : '', // 截断长文本
        outerHTML: element.outerHTML.substring(0, 500), // 截断长HTML
        position: {
            left: rect.left,
            top: rect.top,
            width: rect.width,
            height: rect.height,
            x: rect.x, // 或者 rect.left
            y: rect.y, // 或者 rect.top
            // 考虑滚动条偏移，获取文档绝对位置
            absoluteLeft: rect.left + window.scrollX,
            absoluteTop: rect.top + window.scrollY
        },
        computedStyle: {
            display: computedStyle.display,
            position: computedStyle.position,
            width: computedStyle.width,
            height: computedStyle.height,
            backgroundColor: computedStyle.backgroundColor,
            color: computedStyle.color,
            fontSize: computedStyle.fontSize,
            // ... 还可以添加更多你关心的CSS属性
        }
    };
}

// 更新浮动信息面板
function updateInfoOverlay(element, mouseX, mouseY) {
    if (!element || !element.tagName) {
        infoOverlay.style.display = 'none';
        return;
    }

    const tag = element.tagName.toLowerCase();
    const id = element.id ? `#${element.id}` : '';
    const rect = element.getBoundingClientRect();

    infoOverlay.innerHTML = `
        &lt;${tag}${id}&gt; <br>
        W: ${Math.round(rect.width)}px, H: ${Math.round(rect.height)}px <br>
        X: ${Math.round(rect.left)}px, Y: ${Math.round(rect.top)}px
    `;
    infoOverlay.style.left = `${mouseX + 10}px`; // 鼠标右侧一点
    infoOverlay.style.top = `${mouseY + 10}px`;  // 鼠标下方一点
    infoOverlay.style.display = 'block';
}

// 启用选择模式
function enterPickingMode() {
    if (isPickingMode) return;
    isPickingMode = true;
    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('click', handleClick, true); // 使用捕获阶段，确保先于其他事件
    document.addEventListener('mouseleave', removeHighlight); // 鼠标离开文档时移除高亮
    console.log("进入元素选择模式。点击元素以获取详情。");
    // 可以在这里改变鼠标样式，但需要通过 content_scripts 注入的 CSS 来实现
    document.body.style.cursor = 'crosshair';
}

// 退出选择模式
function exitPickingMode() {
    if (!isPickingMode) return;
    isPickingMode = false;
    document.removeEventListener('mousemove', handleMouseMove);
    document.removeEventListener('click', handleClick, true);
    document.removeEventListener('mouseleave', removeHighlight);
    removeHighlight(); // 移除高亮
    if (currentSelectedElement) {
        currentSelectedElement.classList.remove('plugin-highlight-element-selected');
        currentSelectedElement = null;
    }
    document.body.style.cursor = 'default';
    console.log("退出元素选择模式。");
}

// 监听来自背景脚本的消息
chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
    if (request.type === "startPicking") {
        enterPickingMode();
        sendResponse({ status: "pickingStarted" });
    } else if (request.type === "stopPicking") {
        exitPickingMode();
        sendResponse({ status: "pickingStopped" });
    }
});
