import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: rootWindow
    objectName: "qmlRootWindow"
    width: 760
    height: 540
    visible: true
    title: "QML Demo"

    property string currentUserName: ""
    property string currentRole: "viewer"

    StackLayout {
        id: pageStack
        objectName: "pageStack"
        anchors.fill: parent
        currentIndex: 0

        Item {
            id: loginPage
            objectName: "loginPage"

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                width: 300

                TextField {
                    id: loginNameInput
                    objectName: "loginNameInput"
                    placeholderText: "姓名"
                }
                TextField {
                    id: loginPasswordInput
                    objectName: "loginPasswordInput"
                    placeholderText: "密码"
                    echoMode: TextInput.Password
                }
                ComboBox {
                    id: loginRoleCombo
                    objectName: "loginRoleCombo"
                    model: ["admin", "editor", "viewer"]
                }
                Button {
                    id: loginButton
                    objectName: "loginButton"
                    text: "登录"
                    onClicked: {
                        if (loginNameInput.text.length === 0 || loginPasswordInput.text.length === 0) {
                            loginStatusLabel.text = "登录失败: 请输入完整信息"
                            return
                        }
                        rootWindow.currentUserName = loginNameInput.text
                        rootWindow.currentRole = loginRoleCombo.currentText
                        loginStatusLabel.text = "登录成功"
                        mainUserInfoLabel.text = "当前用户: " + rootWindow.currentUserName + " (" + rootWindow.currentRole + ")"
                        pageStack.currentIndex = 1
                    }
                }
                Label {
                    id: loginStatusLabel
                    objectName: "loginStatusLabel"
                    text: ""
                }
            }
        }

        Item {
            id: mainPage
            objectName: "mainPage"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                Label {
                    id: mainUserInfoLabel
                    objectName: "mainUserInfoLabel"
                    text: "未登录"
                }

                TabBar {
                    id: settingsTabs
                    objectName: "settingsTabs"
                    currentIndex: 0
                    TabButton { text: "用户配置"; objectName: "tabUserConfig" }
                    TabButton { text: "权限配置"; objectName: "tabPermissionConfig" }
                }

                StackLayout {
                    id: settingsPages
                    objectName: "settingsPages"
                    currentIndex: settingsTabs.currentIndex
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Item {
                        id: userConfigPage
                        objectName: "userConfigPage"
                        ColumnLayout {
                            anchors.fill: parent
                            TextField {
                                id: userNicknameInput
                                objectName: "userNicknameInput"
                                placeholderText: "昵称"
                            }
                            TextField {
                                id: userAvatarPathInput
                                objectName: "userAvatarPathInput"
                                placeholderText: "上传文件路径"
                            }
                            Slider {
                                id: userVolumeSlider
                                objectName: "userVolumeSlider"
                                from: 0
                                to: 100
                                value: 50
                            }
                            Button {
                                id: saveUserButton
                                objectName: "saveUserButton"
                                text: "保存用户配置"
                                onClicked: {
                                    let nick = userNicknameInput.text.length === 0 ? "未设置" : userNicknameInput.text
                                    globalMessageLabel.text = "保存成功: " + nick
                                }
                            }
                        }
                    }

                    Item {
                        id: permissionConfigPage
                        objectName: "permissionConfigPage"
                        ColumnLayout {
                            anchors.fill: parent
                            ComboBox {
                                id: permissionRoleCombo
                                objectName: "permissionRoleCombo"
                                model: ["admin", "editor", "viewer"]
                            }
                            Label {
                                id: permissionValueLabel
                                objectName: "permissionValueLabel"
                                text: "当前权限: viewer"
                            }
                            Switch {
                                id: notifyToggle
                                objectName: "notifyToggle"
                                text: "开启通知"
                            }
                            Label {
                                id: toggleStatusLabel
                                objectName: "toggleStatusLabel"
                                text: "通知: " + (notifyToggle.checked ? "开启" : "关闭")
                            }
                            ButtonGroup { id: modeGroup }
                            RadioButton {
                                id: modeAdminRadio
                                objectName: "modeAdminRadio"
                                text: "管理员模式"
                                ButtonGroup.group: modeGroup
                            }
                            RadioButton {
                                id: modeEditorRadio
                                objectName: "modeEditorRadio"
                                text: "编辑模式"
                                ButtonGroup.group: modeGroup
                            }
                            RadioButton {
                                id: modeViewerRadio
                                objectName: "modeViewerRadio"
                                text: "访客模式"
                                checked: true
                                ButtonGroup.group: modeGroup
                            }
                            Label {
                                id: selectedModeLabel
                                objectName: "selectedModeLabel"
                                text: modeAdminRadio.checked ? "当前模式: admin"
                                      : (modeEditorRadio.checked ? "当前模式: editor" : "当前模式: viewer")
                            }
                            CheckBox {
                                id: permReadCheck
                                objectName: "permReadCheck"
                                text: "读权限"
                            }
                            CheckBox {
                                id: permWriteCheck
                                objectName: "permWriteCheck"
                                text: "写权限"
                            }
                            CheckBox {
                                id: permDeleteCheck
                                objectName: "permDeleteCheck"
                                text: "删权限"
                            }
                            Label {
                                id: selectedPermsLabel
                                objectName: "selectedPermsLabel"
                                text: "已选权限: 无"
                            }
                            Item {
                                id: roleMultiCombo
                                objectName: "roleMultiCombo"
                                property var options: ["admin", "editor", "viewer"]
                                property var selectedValues: []
                                implicitHeight: 100
                                implicitWidth: 220
                                Column {
                                    Repeater {
                                        model: roleMultiCombo.options
                                        delegate: CheckBox {
                                            text: modelData
                                            checked: roleMultiCombo.selectedValues.indexOf(modelData) >= 0
                                            onToggled: {
                                                let next = []
                                                for (let i = 0; i < roleMultiCombo.selectedValues.length; i++) {
                                                    if (roleMultiCombo.selectedValues[i] !== modelData) {
                                                        next.push(roleMultiCombo.selectedValues[i])
                                                    }
                                                }
                                                if (checked) {
                                                    next.push(modelData)
                                                }
                                                roleMultiCombo.selectedValues = next
                                            }
                                        }
                                    }
                                }
                            }
                            Label {
                                id: dropdownSelectedLabel
                                objectName: "dropdownSelectedLabel"
                                text: "下拉多选: 无"
                            }
                            Button {
                                id: savePermissionButton
                                objectName: "savePermissionButton"
                                text: "保存权限"
                                onClicked: {
                                    permissionValueLabel.text = "当前权限: " + permissionRoleCombo.currentText
                                    let perms = []
                                    if (permReadCheck.checked) { perms.push("read") }
                                    if (permWriteCheck.checked) { perms.push("write") }
                                    if (permDeleteCheck.checked) { perms.push("delete") }
                                    selectedPermsLabel.text = "已选权限: " + (perms.length ? perms.join(",") : "无")
                                    dropdownSelectedLabel.text = "下拉多选: "
                                            + (roleMultiCombo.selectedValues.length ? roleMultiCombo.selectedValues.join(",") : "无")
                                    globalMessageLabel.text = "权限保存成功"
                                }
                            }
                        }
                    }
                }

                Label {
                    id: globalMessageLabel
                    objectName: "globalMessageLabel"
                    text: ""
                }
            }
        }
    }
}

