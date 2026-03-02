#include "UiAutomationProxyServer.h"

#include <QApplication>
#include <QMainWindow>
#include <QStackedWidget>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QSlider>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStringList>
#include <QMessageBox>

// 用于保存用户状态的结构体
struct UserState {
    QString name;
    QString role = "viewer";
};

class WidgetsDemoWindow : public QMainWindow {
    Q_OBJECT

public:
    WidgetsDemoWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        setObjectName("widgetsMainWindow");
        setWindowTitle("Widgets Demo");
        resize(720, 520);
        _build_ui();
    }

private:
    UserState state;
    QStackedWidget *pageStack;
    QWidget *loginPage;
    QWidget *mainPage;

    // Login Page Widgets
    QLineEdit *nameInput;
    QLineEdit *passwordInput;
    QComboBox *roleCombo;
    QPushButton *loginButton;
    QLabel *loginStatusLabel;

    // Main Page Widgets
    QLabel *userInfoLabel;
    QTabWidget *settingsTabs;
    QLineEdit *nicknameInput;
    QLineEdit *avatarPathInput;
    QSlider *volumeSlider;
    QComboBox *permissionRoleCombo;
    QLabel *permissionValueLabel;
    QCheckBox *notifyToggle;
    QLabel *toggleStatusLabel;
    QButtonGroup *modeGroup;
    QRadioButton *modeAdminRadio;
    QRadioButton *modeEditorRadio;
    QRadioButton *modeViewerRadio;
    QLabel *selectedModeLabel;
    QCheckBox *permReadCheck;
    QCheckBox *permWriteCheck;
    QCheckBox *permDeleteCheck;
    QLabel *selectedPermsLabel;
    QListWidget *roleMultiCombo;
    QLabel *dropdownSelectedLabel;
    QPushButton *saveUserButton;
    QLabel *globalMessageLabel;

    void _build_ui() {
        pageStack = new QStackedWidget(this);
        pageStack->setObjectName("pageStack");
        setCentralWidget(pageStack);

        loginPage = new QWidget();
        loginPage->setObjectName("loginPage");
        mainPage = new QWidget();
        mainPage->setObjectName("mainPage");

        pageStack->addWidget(loginPage);
        pageStack->addWidget(mainPage);

        _build_login_page();
        _build_main_page();
    }

    void _build_login_page() {
        QFormLayout *layout = new QFormLayout(loginPage);
        nameInput = new QLineEdit();
        nameInput->setObjectName("loginNameInput");
        passwordInput = new QLineEdit();
        passwordInput->setObjectName("loginPasswordInput");
        passwordInput->setEchoMode(QLineEdit::Password);
        
        roleCombo = new QComboBox();
        roleCombo->setObjectName("loginRoleCombo");
        roleCombo->addItems({"admin", "editor", "viewer"});
        
        loginButton = new QPushButton("登录");
        loginButton->setObjectName("loginButton");
        
        loginStatusLabel = new QLabel("");
        loginStatusLabel->setObjectName("loginStatusLabel");

        layout->addRow("姓名", nameInput);
        layout->addRow("密码", passwordInput);
        layout->addRow("角色", roleCombo);
        layout->addRow(loginButton);
        layout->addRow("状态", loginStatusLabel);

        connect(loginButton, &QPushButton::clicked, this, &WidgetsDemoWindow::_on_login_clicked);
    }

    void _build_main_page() {
        QVBoxLayout *container = new QVBoxLayout(mainPage);
        userInfoLabel = new QLabel("未登录");
        userInfoLabel->setObjectName("mainUserInfoLabel");
        container->addWidget(userInfoLabel);

        settingsTabs = new QTabWidget();
        settingsTabs->setObjectName("settingsTabs");
        container->addWidget(settingsTabs);

        // --- 用户配置页 ---
        QWidget *userPage = new QWidget();
        userPage->setObjectName("userConfigPage");
        QVBoxLayout *userLayout = new QVBoxLayout(userPage);
        nicknameInput = new QLineEdit();
        nicknameInput->setObjectName("userNicknameInput");
        avatarPathInput = new QLineEdit();
        avatarPathInput->setObjectName("userAvatarPathInput");
        volumeSlider = new QSlider(Qt::Horizontal);
        volumeSlider->setRange(0, 100);
        volumeSlider->setObjectName("userVolumeSlider");
        
        userLayout->addWidget(new QLabel("昵称"));
        userLayout->addWidget(nicknameInput);
        userLayout->addWidget(new QLabel("头像路径"));
        userLayout->addWidget(avatarPathInput);
        userLayout->addWidget(new QLabel("音量"));
        userLayout->addWidget(volumeSlider);

        // --- 权限配置页 ---
        QWidget *permissionPage = new QWidget();
        permissionPage->setObjectName("permissionConfigPage");
        QVBoxLayout *permLayout = new QVBoxLayout(permissionPage);
        
        permissionRoleCombo = new QComboBox();
        permissionRoleCombo->setObjectName("permissionRoleCombo");
        permissionRoleCombo->addItems({"admin", "editor", "viewer"});
        
        permissionValueLabel = new QLabel("当前权限: viewer");
        permissionValueLabel->setObjectName("permissionValueLabel");
        
        QPushButton *savePermissionButton = new QPushButton("保存权限");
        savePermissionButton->setObjectName("savePermissionButton");
        
        notifyToggle = new QCheckBox("开启通知");
        notifyToggle->setObjectName("notifyToggle");
        toggleStatusLabel = new QLabel("通知: 关闭");
        toggleStatusLabel->setObjectName("toggleStatusLabel");

        // Mode Group
        modeGroup = new QButtonGroup(this);
        modeAdminRadio = new QRadioButton("管理员模式");
        modeAdminRadio->setObjectName("modeAdminRadio");
        modeEditorRadio = new QRadioButton("编辑模式");
        modeEditorRadio->setObjectName("modeEditorRadio");
        modeViewerRadio = new QRadioButton("访客模式");
        modeViewerRadio->setObjectName("modeViewerRadio");
        modeViewerRadio->setChecked(true);
        modeGroup->addButton(modeAdminRadio);
        modeGroup->addButton(modeEditorRadio);
        modeGroup->addButton(modeViewerRadio);
        selectedModeLabel = new QLabel("当前模式: viewer");
        selectedModeLabel->setObjectName("selectedModeLabel");

        // Permissions Checkboxes
        permReadCheck = new QCheckBox("读权限");
        permReadCheck->setObjectName("permReadCheck");
        permWriteCheck = new QCheckBox("写权限");
        permWriteCheck->setObjectName("permWriteCheck");
        permDeleteCheck = new QCheckBox("删权限");
        permDeleteCheck->setObjectName("permDeleteCheck");
        selectedPermsLabel = new QLabel("已选权限: 无");
        selectedPermsLabel->setObjectName("selectedPermsLabel");

        // Multi-select list
        roleMultiCombo = new QListWidget();
        roleMultiCombo->setObjectName("roleMultiCombo");
        roleMultiCombo->setSelectionMode(QAbstractItemView::MultiSelection);
        roleMultiCombo->addItems({"admin", "editor", "viewer"});
        dropdownSelectedLabel = new QLabel("下拉多选: 无");
        dropdownSelectedLabel->setObjectName("dropdownSelectedLabel");

        permLayout->addWidget(permissionRoleCombo);
        permLayout->addWidget(permissionValueLabel);
        permLayout->addWidget(notifyToggle);
        permLayout->addWidget(toggleStatusLabel);
        permLayout->addWidget(modeAdminRadio);
        permLayout->addWidget(modeEditorRadio);
        permLayout->addWidget(modeViewerRadio);
        permLayout->addWidget(selectedModeLabel);
        permLayout->addWidget(permReadCheck);
        permLayout->addWidget(permWriteCheck);
        permLayout->addWidget(permDeleteCheck);
        permLayout->addWidget(selectedPermsLabel);
        permLayout->addWidget(roleMultiCombo);
        permLayout->addWidget(dropdownSelectedLabel);
        permLayout->addWidget(savePermissionButton);

        settingsTabs->addTab(userPage, "用户配置");
        settingsTabs->addTab(permissionPage, "权限配置");

        // --- 底部动作栏 ---
        QHBoxLayout *actions = new QHBoxLayout();
        saveUserButton = new QPushButton("保存用户配置");
        saveUserButton->setObjectName("saveUserButton");
        globalMessageLabel = new QLabel("");
        globalMessageLabel->setObjectName("globalMessageLabel");
        actions->addWidget(saveUserButton);
        actions->addWidget(globalMessageLabel);
        container->addLayout(actions);

        // --- 连接信号槽 ---
        connect(savePermissionButton, &QPushButton::clicked, this, &WidgetsDemoWindow::_on_save_permission);
        connect(saveUserButton, &QPushButton::clicked, this, &WidgetsDemoWindow::_on_save_user);
        
        connect(notifyToggle, &QCheckBox::toggled, this, [this](bool checked){
            toggleStatusLabel->setText(checked ? "通知: 开启" : "通知: 关闭");
        });

        connect(modeAdminRadio, &QRadioButton::toggled, this, &WidgetsDemoWindow::_update_mode_label);
        connect(modeEditorRadio, &QRadioButton::toggled, this, &WidgetsDemoWindow::_update_mode_label);
        connect(modeViewerRadio, &QRadioButton::toggled, this, &WidgetsDemoWindow::_update_mode_label);

        connect(permReadCheck, &QCheckBox::toggled, this, &WidgetsDemoWindow::_update_permissions_label);
        connect(permWriteCheck, &QCheckBox::toggled, this, &WidgetsDemoWindow::_update_permissions_label);
        connect(permDeleteCheck, &QCheckBox::toggled, this, &WidgetsDemoWindow::_update_permissions_label);

        connect(roleMultiCombo, &QListWidget::itemSelectionChanged, this, &WidgetsDemoWindow::_update_role_multi_label);
    }

private slots:
    void _on_login_clicked() {
        QString name = nameInput->text().trimmed();
        QString pwd = passwordInput->text().trimmed();
        QString role = roleCombo->currentText();
        
        if (name.isEmpty() || pwd.isEmpty()) {
            loginStatusLabel->setText("登录失败: 请输入完整信息");
            return;
        }
        
        state.name = name;
        state.role = role;
        loginStatusLabel->setText("登录成功");
        userInfoLabel->setText(QString("当前用户: %1 (%2)").arg(name, role));
        pageStack->setCurrentWidget(mainPage);
    }

    void _on_save_user() {
        QString nickname = nicknameInput->text().trimmed();
        if (nickname.isEmpty()) nickname = "未设置";
        globalMessageLabel->setText(QString("保存成功: %1").arg(nickname));
    }

    void _on_save_permission() {
        QString role = permissionRoleCombo->currentText();
        permissionValueLabel->setText(QString("当前权限: %1").arg(role));
        _update_permissions_label();
        _update_role_multi_label();
        globalMessageLabel->setText("权限保存成功");
    }

    void _update_mode_label() {
        if (modeAdminRadio->isChecked()) selectedModeLabel->setText("当前模式: admin");
        else if (modeEditorRadio->isChecked()) selectedModeLabel->setText("当前模式: editor");
        else selectedModeLabel->setText("当前模式: viewer");
    }

    void _update_permissions_label() {
        QStringList selected;
        if (permReadCheck->isChecked()) selected.append("read");
        if (permWriteCheck->isChecked()) selected.append("write");
        if (permDeleteCheck->isChecked()) selected.append("delete");
        
        selectedPermsLabel->setText(QString("已选权限: %1").arg(selected.isEmpty() ? "无" : selected.join(",")));
    }

    void _update_role_multi_label() {
        QStringList selected;
        QList<QListWidgetItem*> items = roleMultiCombo->selectedItems();
        for (QListWidgetItem* item : items) {
            selected.append(item->text());
        }
        dropdownSelectedLabel->setText(QString("下拉多选: %1").arg(selected.isEmpty() ? "无" : selected.join(",")));
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    WidgetsDemoWindow window;
    window.show();

    UiAutomationProxyServer proxy;
    proxy.useDefaultQtHandler(&window);
    proxy.start(12345, QHostAddress::LocalHost, QStringLiteral("demo-token"));
    
    return app.exec();
}

#include "main.moc"