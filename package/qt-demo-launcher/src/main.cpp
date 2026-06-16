#include "config.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>
#include <QtWidgets/QMessageBox>
#include <QProcess>
#include <QDir>
#include <QFont>
#include <QDesktopWidget>
#include <QStyleFactory>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QPixmap>
#include <QIcon>
#include <QFileSystemWatcher>
#include <QScreen>
#include <QMap>
#include "NetworkInterface.h"

struct ButtonConfig {
    QString id;
    bool enabled = true;
    QString text;
    QString iconPath;
    QString program;
    QStringList arguments;
    QString workingDirectory = "/tmp";
    QString action; // "quit" for special actions
    QString page = "home";
    QString targetPage;
    QString pageTitle;
    bool detached = false;

    // Position and size
    int row = 0;
    int column = 0;
    int columnSpan = 1;
    int rowSpan = 1;
    int width = 200;
    int height = 100;

    // Icon settings
    int iconWidth = 48;
    int iconHeight = 48;
    QString iconLayout = "icon_left"; // icon_left, icon_right, icon_top, icon_only, text_only

    // Styling
    int fontSize = 24;
    QString backgroundColor = "#404040";
    QString hoverColor = "#505050";
    int borderRadius = 15;
};

struct TitleConfig {
    QString text = "Touch Applications";
    QString logoPath;
    int logoWidth = 150;
    int logoHeight = 60;
    QString layout = "text_only"; // logo_left, logo_right, logo_top, logo_only, text_only
    int fontSize = 32;
    QString color = "#ffffff";
};

struct LayoutConfig {
    QString type = "vertical"; // grid, vertical, horizontal
    int columns = 2;
    int rows = 2;
    int spacing = 15;
    int topMargin = 50;
    int bottomMargin = 50;
    int leftMargin = 50;
    int rightMargin = 50;
    bool dynamicLayout = true;  // Dynamic sizing to fill screen (can be disabled)
};

struct LauncherConfig {
    TitleConfig title;
    LayoutConfig layout;
    QList<ButtonConfig> buttons;
    int windowWidth = 800;
    int windowHeight = 600;
};

class TouchAppLauncher : public QMainWindow
{
    Q_OBJECT

public:
    TouchAppLauncher(const QString &configPath = QString(), int networkPort = DEFAULT_LAUNCHER_PORT,
                     bool forceFixedLayout = false, QWidget *parent = nullptr)
      : QMainWindow(parent), m_configWatcher(nullptr), m_currentConfigFile(""),
        m_configPath(configPath), m_networkPort(networkPort),
        m_networkInterface(nullptr), m_runningProcess(nullptr), m_runningAppId(""),
        m_currentPage("home"),
        m_scaleFactor(1.0), m_forceFixedLayout(forceFixedLayout),
        m_dynamicButtonWidth(0), m_dynamicButtonHeight(0)
    {
        loadConfig();
        calculateScaleFactor();
        calculateDynamicSizes();
        setupUI();
        validatePrograms();
        setupNetworkInterface();
        setupConfigWatcher();
    }

private slots:
    void buttonClicked()
    {
        QPushButton *button = qobject_cast<QPushButton*>(sender());
        if (!button) return;

        QString buttonId = button->property("buttonId").toString();

        // Find button config
        ButtonConfig config;
        bool found = false;
        for (const auto &btn : m_config.buttons) {
            if (btn.id == buttonId) {
                config = btn;
                found = true;
                break;
            }
        }

        if (!found) {
            qWarning() << "Button config not found for ID:" << buttonId;
            return;
        }

        // Handle special actions
        QString action = config.action.toLower();
        if (action == "quit") {
            QApplication::quit();
            return;
        }
        if (action == "navigate") {
            if (config.targetPage.isEmpty()) {
                qWarning() << "Navigation button has no target_page:" << config.id;
                return;
            }
            navigateToPage(config.targetPage);
            return;
        }
        if (action == "back") {
            navigateBack();
            return;
        }
        if (action == "home") {
            navigateHome();
            return;
        }

        // Launch program
        if (!config.program.isEmpty()) {
            if (config.detached) {
                QString errorMessage;
                if (!launchDetached(config.program, config.arguments,
                                    config.workingDirectory, buttonId, &errorMessage)) {
                    QMessageBox::warning(this, "Error", errorMessage);
                }
            } else {
                QProcessEnvironment env = createTouchEnvironment();
                launchApp(config.program, config.arguments, env, config.workingDirectory, buttonId);
            }
        }
    }

    // Add file monitoring setup method:
    void setupConfigWatcher()
    {
	    if (!m_currentConfigFile.isEmpty()) {
		m_configWatcher = new QFileSystemWatcher(this);
		m_configWatcher->addPath(m_currentConfigFile);
		connect(m_configWatcher, &QFileSystemWatcher::fileChanged,
			this, &TouchAppLauncher::reloadConfiguration);
		qDebug() << "File watcher setup for:" << m_currentConfigFile;
	    } else {
		qDebug() << "No config file to monitor - using defaults";
	    }
    }

    // Add configuration reload method:
    void reloadConfiguration()
    {
	    qDebug() << "Config file changed, reloading...";

	    // Reload configuration from file
	    loadConfig();

	    // Refresh the UI with new config
	    calculateScaleFactor();
	    refreshUI();

	    // Re-add file to watcher (some systems remove it after change)
	    if (m_configWatcher && !m_currentConfigFile.isEmpty()) {
		if (!m_configWatcher->files().contains(m_currentConfigFile)) {
		    m_configWatcher->addPath(m_currentConfigFile);
		    qDebug() << "Re-added file to watcher:" << m_currentConfigFile;
		}
	    }

	    qDebug() << "Configuration reload complete";
    }

    // Add UI refresh method:
    void refreshUI()
    {
	    QWidget *centralWidget = this->centralWidget();
	    if (!centralWidget) return;

	    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(centralWidget->layout());
	    if (!mainLayout) return;

	    calculateScaleFactor();
	    calculateDynamicSizes();
	    applyMainLayoutMetrics(mainLayout);

	    // Remove existing title and button layouts.
	    while (mainLayout->count() > 0) {
		QLayoutItem *item = mainLayout->takeAt(0);
		deleteLayoutRecursively(item);
	    }

	    QWidget *titleWidget = createTitleWidget();
	    if (titleWidget) {
		mainLayout->addWidget(titleWidget);
	    }

	    // Create new button layout with current page
	    QLayout *buttonLayout = createButtonLayout();
	    if (buttonLayout) {
		mainLayout->addLayout(buttonLayout);
	    }

	    mainLayout->invalidate();
	    mainLayout->activate();
	    centralWidget->updateGeometry();
	    centralWidget->update();
	    update();

	    qDebug() << "UI refresh complete";
    }

    // Add recursive layout deletion helper:
    void deleteLayoutRecursively(QLayoutItem *item)
    {
	    if (!item) return;

	    if (QLayout *layout = item->layout()) {
		// Recursively delete child layouts
		while (QLayoutItem *child = layout->takeAt(0)) {
		    deleteLayoutRecursively(child);
		}
	    } else if (QWidget *widget = item->widget()) {
		// Delete widget
		widget->deleteLater();
	    }

	    delete item;
    }

    void handleNetworkCommand(const QString &command)
    {
        qDebug() << "Network command received:" << command;

        QStringList parts = command.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            m_networkInterface->sendResponse("ERROR: Empty command");
            return;
        }

        QString cmd = parts[0].toLower();

        if (cmd == "list-apps") {
            QString appList = listApps();
            m_networkInterface->sendResponse(appList);
        } else if (cmd == "list-page-apps") {
            QString pageId = parts.size() >= 2 ? parts[1] : m_currentPage;
            if (!pageExists(pageId)) {
                m_networkInterface->sendResponse("ERROR: page-not-found");
            } else {
                m_networkInterface->sendResponse(listPageApps(pageId));
            }
        } else if (cmd == "get-page") {
            m_networkInterface->sendResponse(m_currentPage);
        } else if (cmd == "navigate" && parts.size() >= 2) {
            if (navigateToPage(parts[1])) {
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: page-not-found");
            }
        } else if (cmd == "back") {
            navigateBack();
            m_networkInterface->sendResponse("OK");
        } else if (cmd == "home") {
            navigateHome();
            m_networkInterface->sendResponse("OK");
        } else if (cmd == "start-app" && parts.size() >= 2) {
            QString appId = parts[1];
            bool result = startApp(appId);
            if (result) {
                m_networkInterface->sendResponse("OK");
            }
            // Error response is sent by startApp() function
        } else if (cmd == "stop-app") {
            bool result = stopApp();
            if (result) {
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: no-app-running");
            }
        } else if (cmd == "get-running-app") {
            QString runningApp = getRunningApp();
            m_networkInterface->sendResponse(runningApp);
        }

	// NEW COMMANDS FOR RUNTIME CONTROL:
        else if (cmd == "reload-config") {
        if (!m_currentConfigFile.isEmpty()) {
            reloadConfiguration();
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: no-config-file");
        }
        } else if (cmd == "set-button-enabled" && parts.size() >= 3) {
        QString buttonId = parts[1];
        bool enabled = (parts[2].toLower() == "true");
        bool result = setButtonEnabled(buttonId, enabled);
        if (result) {
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: button-not-found");
        }
        } else if (cmd == "get-button-status" && parts.size() >= 2) {
        QString buttonId = parts[1];
        QString status = getButtonStatus(buttonId);
        m_networkInterface->sendResponse(status);
        } else if (cmd == "list-all-buttons") {
        QStringList allButtons;
        for (const ButtonConfig &config : m_config.buttons) {
            QString status = config.enabled ? "enabled" : "disabled";
            allButtons << QString("%1:%2").arg(config.id, status);
        }
        m_networkInterface->sendResponse(allButtons.join(","));
        } else {
            m_networkInterface->sendResponse("ERROR: Unknown command");
        }
    }

private:
    QFileSystemWatcher *m_configWatcher;
    QString m_currentConfigFile;
    LauncherConfig m_config;
    QString m_configPath;
    int m_networkPort;
    NetworkInterface *m_networkInterface;
    QProcess *m_runningProcess;
    QString m_runningAppId;
    QString m_currentPage;
    QStringList m_pageStack;
    QMap<QString, QString> m_pageTitles;
    double m_scaleFactor;  // Scale factor for responsive layout
    bool m_forceFixedLayout;  // CLI override to disable dynamic layout
    int m_dynamicButtonWidth;  // Calculated button width for dynamic layout
    int m_dynamicButtonHeight; // Calculated button height for dynamic layout
    int m_titleHeight;  // Estimated title widget height

    void calculateScaleFactor()
    {
        // Get actual screen size
        QScreen *screen = QApplication::primaryScreen();
        if (!screen) {
            qWarning() << "Could not get primary screen, using scale factor 1.0";
            m_scaleFactor = 1.0;
            return;
        }

        QSize screenSize = screen->size();
        int screenWidth = screenSize.width();
        int screenHeight = screenSize.height();

        // Calculate scale factors based on config window size
        double scaleX = (double)screenWidth / m_config.windowWidth;
        double scaleY = (double)screenHeight / m_config.windowHeight;

        // Use the smaller scale to ensure everything fits
        m_scaleFactor = qMin(scaleX, scaleY);

        // Clamp scale factor to reasonable range (0.3 to 1.5)
        m_scaleFactor = qBound(0.3, m_scaleFactor, 1.5);

        qDebug() << "Screen size:" << screenWidth << "x" << screenHeight;
        qDebug() << "Config window size:" << m_config.windowWidth << "x" << m_config.windowHeight;
        qDebug() << "Scale factors - X:" << scaleX << "Y:" << scaleY << "Using:" << m_scaleFactor;
    }

    void calculateDynamicSizes()
    {
        // Check if dynamic layout is enabled
        bool useDynamic = m_config.layout.dynamicLayout && !m_forceFixedLayout;

        if (!useDynamic) {
            qDebug() << "Dynamic layout disabled, using fixed scaled sizes";
            m_dynamicButtonWidth = 0;
            m_dynamicButtonHeight = 0;
            return;
        }

        // Get screen size
        QScreen *screen = QApplication::primaryScreen();
        if (!screen) {
            qWarning() << "Could not get primary screen for dynamic sizing";
            return;
        }

        QSize screenSize = screen->size();
        int screenWidth = screenSize.width();
        int screenHeight = screenSize.height();

        // Calculate scaled margins
        int scaledTopMargin = (int)(m_config.layout.topMargin * m_scaleFactor);
        int scaledBottomMargin = (int)(m_config.layout.bottomMargin * m_scaleFactor);
        int scaledLeftMargin = (int)(m_config.layout.leftMargin * m_scaleFactor);
        int scaledRightMargin = (int)(m_config.layout.rightMargin * m_scaleFactor);
        int scaledSpacing = (int)(m_config.layout.spacing * m_scaleFactor);
        int scaledMainSpacing = (int)(20 * m_scaleFactor);

        // Estimate title height (font size + padding)
        int scaledTitleFontSize = qMax(16, (int)(m_config.title.fontSize * m_scaleFactor));
        int scaledTitlePadding = qMax(5, (int)(15 * m_scaleFactor));
        m_titleHeight = scaledTitleFontSize + scaledTitlePadding * 2 + scaledMainSpacing;

        // Calculate available content area
        int availableWidth = screenWidth - scaledLeftMargin - scaledRightMargin;
        int availableHeight = screenHeight - scaledTopMargin - scaledBottomMargin - m_titleHeight;

        // For grid layout, calculate button sizes to fill the grid
        if (m_config.layout.type == "grid") {
            int cols = m_config.layout.columns;
            int rows = m_config.layout.rows;

            // Account for spacing between buttons
            int totalHSpacing = (cols - 1) * scaledSpacing;
            int totalVSpacing = (rows - 1) * scaledSpacing;

            m_dynamicButtonWidth = (availableWidth - totalHSpacing) / cols;
            m_dynamicButtonHeight = (availableHeight - totalVSpacing) / rows;

            // Ensure minimum reasonable sizes
            m_dynamicButtonWidth = qMax(100, m_dynamicButtonWidth);
            m_dynamicButtonHeight = qMax(60, m_dynamicButtonHeight);

            qDebug() << "Dynamic layout enabled:";
            qDebug() << "  Available area:" << availableWidth << "x" << availableHeight;
            qDebug() << "  Grid:" << cols << "x" << rows;
            qDebug() << "  Dynamic button size:" << m_dynamicButtonWidth << "x" << m_dynamicButtonHeight;
        } else {
            // For vertical/horizontal layouts, use available width and distribute height
            m_dynamicButtonWidth = availableWidth;

            // Count enabled buttons
            int enabledCount = buttonsForPage(m_currentPage).count();

            if (enabledCount > 0) {
                int totalVSpacing = (enabledCount - 1) * scaledSpacing;
                m_dynamicButtonHeight = (availableHeight - totalVSpacing) / enabledCount;
                m_dynamicButtonHeight = qMax(60, m_dynamicButtonHeight);
            }

            qDebug() << "Dynamic layout (vertical/horizontal):";
            qDebug() << "  Dynamic button size:" << m_dynamicButtonWidth << "x" << m_dynamicButtonHeight;
        }
    }

    void setupNetworkInterface()
    {
        m_networkInterface = new NetworkInterface(m_networkPort, this);
        connect(m_networkInterface, &NetworkInterface::commandReceived,
                this, &TouchAppLauncher::handleNetworkCommand);

        if (m_networkInterface->startServer()) {
            qDebug() << "Network interface started on port" << m_networkPort;
        } else {
            qWarning() << "Failed to start network interface on port" << m_networkPort;
        }
    }

    void applyMainLayoutMetrics(QVBoxLayout *mainLayout)
    {
        if (!mainLayout) return;

        int scaledTopMargin = (int)(m_config.layout.topMargin * m_scaleFactor);
        int scaledBottomMargin = (int)(m_config.layout.bottomMargin * m_scaleFactor);
        int scaledLeftMargin = (int)(m_config.layout.leftMargin * m_scaleFactor);
        int scaledRightMargin = (int)(m_config.layout.rightMargin * m_scaleFactor);

        mainLayout->setSpacing((int)(20 * m_scaleFactor));
        mainLayout->setContentsMargins(scaledLeftMargin, scaledTopMargin,
                                      scaledRightMargin, scaledBottomMargin);
    }

    QString normalizePageId(const QString &pageId) const
    {
        return pageId.isEmpty() ? QString("home") : pageId;
    }

    bool findButton(const QString &buttonId, ButtonConfig &button, bool enabledOnly = false) const
    {
        for (const ButtonConfig &config : m_config.buttons) {
            if (config.id == buttonId && (!enabledOnly || config.enabled)) {
                button = config;
                return true;
            }
        }
        return false;
    }

    QList<ButtonConfig> buttonsForPage(const QString &pageId, bool enabledOnly = true) const
    {
        QList<ButtonConfig> buttons;
        QString normalizedPage = normalizePageId(pageId);

        for (const ButtonConfig &config : m_config.buttons) {
            if (config.page == normalizedPage && (!enabledOnly || config.enabled)) {
                buttons.append(config);
            }
        }

        return buttons;
    }

    bool pageExists(const QString &pageId) const
    {
        QString normalizedPage = normalizePageId(pageId);
        if (normalizedPage == "home") {
            return true;
        }

        if (m_pageTitles.contains(normalizedPage)) {
            return true;
        }

        for (const ButtonConfig &config : m_config.buttons) {
            if (config.page == normalizedPage || config.targetPage == normalizedPage) {
                return true;
            }
        }

        return false;
    }

    QString currentTitleText() const
    {
        if (m_currentPage == "home") {
            return m_config.title.text;
        }

        return m_pageTitles.value(m_currentPage, m_currentPage);
    }

    bool navigateToPage(const QString &pageId, bool pushHistory = true)
    {
        QString normalizedPage = normalizePageId(pageId);
        if (!pageExists(normalizedPage)) {
            qWarning() << "Page not found:" << normalizedPage;
            return false;
        }

        if (normalizedPage == m_currentPage) {
            return true;
        }

        if (pushHistory) {
            m_pageStack.append(m_currentPage);
        }

        m_currentPage = normalizedPage;
        refreshUI();
        return true;
    }

    void navigateBack()
    {
        if (!m_pageStack.isEmpty()) {
            m_currentPage = m_pageStack.takeLast();
        } else {
            m_currentPage = "home";
        }

        refreshUI();
    }

    void navigateHome()
    {
        m_pageStack.clear();
        m_currentPage = "home";
        refreshUI();
    }

    QString listApps()
    {
        QStringList appIds;
        for (const ButtonConfig &config : m_config.buttons) {
            if (config.enabled) {
                appIds << config.id;
            }
        }
        return appIds.join(",");
    }

    QString listPageApps(const QString &pageId)
    {
        QStringList appIds;
        for (const ButtonConfig &config : buttonsForPage(pageId)) {
            appIds << config.id;
        }
        return appIds.join(",");
    }

    bool startApp(const QString &appId)
    {
        // Check if an app is already running
        if (m_runningProcess && m_runningProcess->state() == QProcess::Running) {
            m_networkInterface->sendResponse("ERROR: app-already-running");
            return false;
        }

        // Find the app configuration across all pages for backward-compatible automation.
        ButtonConfig config;
        if (!findButton(appId, config, true)) {
            m_networkInterface->sendResponse("ERROR: invalid-app-name");
            return false;
        }

        // Handle special actions
        QString action = config.action.toLower();
        if (action == "quit") {
            m_networkInterface->sendResponse("OK");
            QApplication::quit();
            return true;
        }
        if (action == "navigate") {
            if (config.targetPage.isEmpty() || !navigateToPage(config.targetPage)) {
                m_networkInterface->sendResponse("ERROR: page-not-found");
                return false;
            }
            return true;
        }
        if (action == "back") {
            navigateBack();
            return true;
        }
        if (action == "home") {
            navigateHome();
            return true;
        }

        // Validate program exists before launching
        if (config.program.isEmpty()) {
            m_networkInterface->sendResponse("ERROR: app-has-no-program");
            return false;
        }

        if (!QFile::exists(config.program)) {
            m_networkInterface->sendResponse("ERROR: program-not-found");
            return false;
        }

        if (config.detached) {
            QString errorMessage;
            if (!launchDetached(config.program, config.arguments,
                                config.workingDirectory, appId, &errorMessage)) {
                m_networkInterface->sendResponse(QString("ERROR: %1").arg(errorMessage));
                return false;
            }

            m_networkInterface->sendResponse("OK");
            return true;
        }

        // Launch the app asynchronously
        m_networkInterface->sendResponse("OK");
        launchAppAsync(config.program, config.arguments, config.workingDirectory, appId);
        return true;
    }

    QString getRunningApp()
    {
        if (m_runningProcess && m_runningProcess->state() == QProcess::Running) {
            return m_runningAppId;
        }
        return "none";
    }

    bool stopApp()
    {
        // Check if an app is currently running
        if (!m_runningProcess || m_runningProcess->state() != QProcess::Running) {
            return false; // No app running
        }

        qDebug() << "Stopping app:" << m_runningAppId;

        // First try graceful termination (SIGTERM)
        m_runningProcess->terminate();

        // Wait up to 3 seconds for graceful shutdown
        if (m_runningProcess->waitForFinished(3000)) {
            qDebug() << "App terminated gracefully:" << m_runningAppId;
        } else {
            // If app doesn't respond to SIGTERM, force kill it
            qWarning() << "App didn't respond to SIGTERM, force killing:" << m_runningAppId;
            m_runningProcess->kill();

            // Wait another 2 seconds for force kill to complete
            if (!m_runningProcess->waitForFinished(2000)) {
                qWarning() << "Failed to force kill app:" << m_runningAppId;
                // Process cleanup will happen via finished() signal eventually
            } else {
                qDebug() << "App force killed:" << m_runningAppId;
            }
        }

        // Note: Process cleanup and state reset happens automatically
        // via the finished() signal handler we already connected in launchAppAsync()

        return true;
    }

    void loadConfig()
    {
        // Determine config file path with priority:
        // 1. Command line argument
        // 2. /etc/launcher.json (default)
        // 3. ./launcher.json (current directory fallback)
        // 4. Built-in defaults

        QString configFile = m_configPath;

        if (configFile.isEmpty()) {
            // Try default locations
            if (QFile::exists("/etc/launcher.json")) {
                configFile = "/etc/launcher.json";
            } else if (QFile::exists("./launcher.json")) {
                configFile = "./launcher.json";
                qDebug() << "Using local launcher.json from current directory";
            }
        }

        if (configFile.isEmpty() || !QFile::exists(configFile)) {
            qWarning() << "No JSON config found, using default configuration";
            qWarning() << "Searched paths:";
            qWarning() << "  - Command line argument:" << (m_configPath.isEmpty() ? "none" : m_configPath);
            qWarning() << "  - /etc/launcher.json";
            qWarning() << "  - ./launcher.json";
            m_currentConfigFile = "";
            loadDefaultConfig();
            return;
        }

        qDebug() << "Loading configuration from:" << configFile;
        m_currentConfigFile = configFile;

        QFile file(configFile);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Could not open config file:" << configFile;
            loadDefaultConfig();
            return;
        }

        QByteArray data = file.readAll();
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);

        if (error.error != QJsonParseError::NoError) {
            qWarning() << "JSON parse error in" << configFile << ":" << error.errorString();
            qWarning() << "Falling back to default configuration";
            loadDefaultConfig();
            return;
        }

        parseJsonConfig(doc.object());
        qDebug() << "Successfully loaded configuration from:" << configFile;
    }

    void parseJsonConfig(const QJsonObject &root)
    {
        m_config = LauncherConfig();
        m_pageTitles.clear();

        QJsonObject launcher = root["launcher"].toObject();

        // Parse window settings
        QJsonObject window = launcher["window"].toObject();
        m_config.windowWidth = window["width"].toInt(800);
        m_config.windowHeight = window["height"].toInt(600);

        // Parse title configuration
        QJsonObject title = launcher["title"].toObject();
        m_config.title.text = title["text"].toString("Touch Applications");
        m_config.title.logoPath = title["logo"].toString();

        QJsonObject logoSize = title["logo_size"].toObject();
        m_config.title.logoWidth = logoSize["width"].toInt(150);
        m_config.title.logoHeight = logoSize["height"].toInt(60);

        m_config.title.layout = title["layout"].toString("text_only");
        m_config.title.fontSize = title["font_size"].toInt(32);
        m_config.title.color = title["color"].toString("#ffffff");

        // Parse layout configuration
        QJsonObject layout = launcher["layout"].toObject();
        m_config.layout.type = layout["type"].toString("vertical");
        m_config.layout.columns = layout["columns"].toInt(2);
        m_config.layout.rows = layout["rows"].toInt(2);
        m_config.layout.spacing = layout["spacing"].toInt(15);

        QJsonObject margins = layout["margins"].toObject();
        m_config.layout.topMargin = margins["top"].toInt(50);
        m_config.layout.bottomMargin = margins["bottom"].toInt(50);
        m_config.layout.leftMargin = margins["left"].toInt(50);
        m_config.layout.rightMargin = margins["right"].toInt(50);

        // Parse dynamic layout option (default: true for smart sizing)
        m_config.layout.dynamicLayout = layout["dynamic_layout"].toBool(true);

        // Parse buttons
        QJsonArray buttons = launcher["buttons"].toArray();
        for (const QJsonValue &value : buttons) {
            QJsonObject btnObj = value.toObject();
            ButtonConfig btn;

            btn.id = btnObj["id"].toString();
            btn.enabled = btnObj["enabled"].toBool(true);
            btn.text = btnObj["text"].toString();
            btn.iconPath = btnObj["icon"].toString();
            btn.program = btnObj["program"].toString();
            btn.workingDirectory = btnObj["working_directory"].toString("/tmp");
            btn.action = btnObj["action"].toString();
            btn.page = normalizePageId(btnObj["page"].toString("home"));
            btn.targetPage = btnObj["target_page"].toString();
            btn.pageTitle = btnObj["page_title"].toString();
            btn.detached = btnObj["detached"].toBool(false)
                || btnObj["launch_mode"].toString().toLower() == "detached";

            // Parse arguments array
            QJsonArray args = btnObj["arguments"].toArray();
            for (const QJsonValue &arg : args) {
                btn.arguments << arg.toString();
            }

            // Parse position
            QJsonObject pos = btnObj["position"].toObject();
            btn.row = pos["row"].toInt(0);
            btn.column = pos["column"].toInt(0);
            btn.columnSpan = pos["column_span"].toInt(1);
            btn.rowSpan = pos["row_span"].toInt(1);

            // Parse size
            QJsonObject size = btnObj["size"].toObject();
            btn.width = size["width"].toInt(200);
            btn.height = size["height"].toInt(100);

            // Parse icon settings
            QJsonObject iconSize = btnObj["icon_size"].toObject();
            btn.iconWidth = iconSize["width"].toInt(48);
            btn.iconHeight = iconSize["height"].toInt(48);
            btn.iconLayout = btnObj["icon_layout"].toString("icon_left");

            // Parse styling
            btn.fontSize = btnObj["font_size"].toInt(24);
            btn.backgroundColor = btnObj["background_color"].toString("#404040");
            btn.hoverColor = btnObj["hover_color"].toString("#505050");
            btn.borderRadius = btnObj["border_radius"].toInt(15);

            if (btn.action.toLower() == "navigate") {
                if (btn.targetPage.isEmpty()) {
                    btn.targetPage = btn.id;
                }
                btn.targetPage = normalizePageId(btn.targetPage);
                QString pageTitle = btn.pageTitle.isEmpty() ? btn.text : btn.pageTitle;
                m_pageTitles.insert(btn.targetPage, pageTitle);
            }

            // Add all buttons (including disabled ones) to config for network API
            m_config.buttons.append(btn);
        }

        QStringList validStack;
        for (const QString &pageId : m_pageStack) {
            if (pageExists(pageId)) {
                validStack.append(pageId);
            }
        }
        m_pageStack = validStack;

        if (!pageExists(m_currentPage)) {
            m_currentPage = "home";
            m_pageStack.clear();
        }
    }

    void loadDefaultConfig()
    {
        // Fallback to hardcoded configuration
        m_config = LauncherConfig();
        m_pageTitles.clear();
        m_pageStack.clear();
        m_currentPage = "home";

        ButtonConfig fingerPaint;
        fingerPaint.id = "fingerpaint";
        fingerPaint.text = "🎨 Finger Paint";
        fingerPaint.program = "/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint";
        fingerPaint.row = 0; fingerPaint.column = 0;

        ButtonConfig scribble;
        scribble.id = "scribble";
        scribble.text = "✏️ Scribble";
        scribble.program = "/usr/lib/qt/examples/widgets/widgets/scribble/scribble";
        scribble.row = 1; scribble.column = 0;

        ButtonConfig gallery;
        gallery.id = "gallery";
        gallery.text = "📷 Photo Gallery";
        gallery.program = "/usr/bin/touch-gallery";
        gallery.arguments << "/Pictures";
        gallery.workingDirectory = "/Pictures";
        gallery.row = 2; gallery.column = 0;

        ButtonConfig slideshow;
        slideshow.id = "slideshow";
        slideshow.text = "🎞️ Slideshow";
        slideshow.program = "/usr/bin/touch-gallery";
        slideshow.arguments << "/Pictures" << "slideshow" << "5";
        slideshow.workingDirectory = "/Pictures";
        slideshow.backgroundColor = "#2e8b57";
        slideshow.hoverColor = "#3cb371";
        slideshow.row = 3; slideshow.column = 0;

        ButtonConfig exit;
        exit.id = "exit";
        exit.text = "❌ Exit";
        exit.action = "quit";
        exit.backgroundColor = "#8b0000";
        exit.hoverColor = "#a00000";
        exit.height = 80;
        exit.row = 4; exit.column = 0;

        m_config.buttons << fingerPaint << scribble << gallery << slideshow << exit;
    }

    QProcessEnvironment createTouchEnvironment()
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

        // Child apps inherit QT_QPA_PLATFORM from parent (systemEnvironment)
        // No need to explicitly set it - this allows both linuxfb and eglfs to work

        // Only set touch device if not already set by system environment
        // This allows init scripts or systemd to configure the correct device
        if (!env.contains("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS")) {
            env.insert("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS", "/dev/input/event0");
        }

        // Set font directory if not already set
        if (!env.contains("QT_QPA_FONTDIR")) {
            env.insert("QT_QPA_FONTDIR", "/usr/share/fonts/dejavu/");
        }

        // Set runtime directory if not already set
        if (!env.contains("XDG_RUNTIME_DIR")) {
            env.insert("XDG_RUNTIME_DIR", "/tmp/runtime-root");
        }

        return env;
    }

    void launchAppAsync(const QString &program, const QStringList &args,
                       const QString &workingDir, const QString &appId)
    {
        // Create runtime directory if needed
        QDir().mkpath("/tmp/runtime-root");

        // Clean up any previous process
        if (m_runningProcess) {
            m_runningProcess->deleteLater();
            m_runningProcess = nullptr;
        }

        // Create process environment
        QProcessEnvironment env = createTouchEnvironment();

        m_runningProcess = new QProcess(this);
        m_runningProcess->setProcessEnvironment(env);
        m_runningProcess->setWorkingDirectory(workingDir);

        // Connect to process signals for async handling
        connect(m_runningProcess, &QProcess::started,
                [this, program, appId]() {
                    m_runningAppId = appId;
                    qDebug() << "App started successfully:" << program << "App ID:" << appId;
                    // Hide launcher after successful start
                    this->hide();
                });

        connect(m_runningProcess, &QProcess::errorOccurred,
                [this, program, appId](QProcess::ProcessError error) {
                    qWarning() << "Failed to start app:" << program << "App ID:" << appId << "Error:" << error;
                    // Don't send network response here - connection already closed
                    m_runningAppId = "";
                    if (m_runningProcess) {
                        m_runningProcess->deleteLater();
                        m_runningProcess = nullptr;
                    }
                });

        // Show launcher again when app exits
        connect(m_runningProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this, program, appId](int exitCode, QProcess::ExitStatus /*exitStatus*/) {
                    qDebug() << "App finished:" << program << "App ID:" << appId << "Exit code:" << exitCode;
                    this->show();
                    m_runningAppId = "";
                    if (m_runningProcess) {
                        m_runningProcess->deleteLater();
                        m_runningProcess = nullptr;
                    }
                });

        // Start the process (non-blocking)
        m_runningProcess->start(program, args);

        qDebug() << "Process start initiated for:" << program << "with args:" << args << "App ID:" << appId;
    }

    bool launchApp(const QString &program, const QStringList &args,
                   const QProcessEnvironment &env, const QString &workingDir, const QString &appId)
    {
        // This is the original synchronous version for button clicks
        // Check if program exists
        if (!QFile::exists(program)) {
            QMessageBox::warning(this, "Error",
                QString("Program not found: %1").arg(program));
            return false;
        }

        // Create runtime directory if needed
        QDir().mkpath("/tmp/runtime-root");

        // Clean up any previous process
        if (m_runningProcess) {
            m_runningProcess->deleteLater();
            m_runningProcess = nullptr;
        }

        m_runningProcess = new QProcess(this);
        m_runningProcess->setProcessEnvironment(env);
        m_runningProcess->setWorkingDirectory(workingDir);

        // Launch the program
        m_runningProcess->start(program, args);

        if (!m_runningProcess->waitForStarted(3000)) {
            QString errorMsg = QString("Failed to start: %1\nError: %2").arg(program, m_runningProcess->errorString());
            QMessageBox::warning(this, "Error", errorMsg);
            m_runningProcess->deleteLater();
            m_runningProcess = nullptr;
            return false;
        }

        m_runningAppId = appId;
        qDebug() << "Launched:" << program << "with args:" << args << "App ID:" << appId;

        // Hide launcher while app is running
        this->hide();

        // Show launcher again when app exits
        connect(m_runningProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this, program, appId](int exitCode, QProcess::ExitStatus /*exitStatus*/) {
                    qDebug() << "App finished:" << program << "App ID:" << appId << "Exit code:" << exitCode;
                    this->show();
                    m_runningAppId = "";
                    if (m_runningProcess) {
                        m_runningProcess->deleteLater();
                        m_runningProcess = nullptr;
                    }
                });

        return true;
    }

    bool launchDetached(const QString &program, const QStringList &args,
                        const QString &workingDir, const QString &appId,
                        QString *errorMessage = nullptr)
    {
        if (!QFile::exists(program)) {
            if (errorMessage) {
                *errorMessage = QString("Program not found: %1").arg(program);
            }
            return false;
        }

        qint64 pid = 0;
        if (!QProcess::startDetached(program, args, workingDir, &pid)) {
            if (errorMessage) {
                *errorMessage = QString("Failed to start detached app: %1").arg(program);
            }
            return false;
        }

        qDebug() << "Detached app started:" << program << "with args:" << args
                 << "App ID:" << appId << "PID:" << pid;
        return true;
    }

    void setupUI()
    {
        setWindowTitle("Touch App Launcher");
        setStyleSheet("QMainWindow { background-color: #000000; }");

        QWidget *centralWidget = new QWidget;
        setCentralWidget(centralWidget);

        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        applyMainLayoutMetrics(mainLayout);

        // Create title section
        QWidget *titleWidget = createTitleWidget();
        if (titleWidget) {
            mainLayout->addWidget(titleWidget);
        }

        // Create buttons layout - only for enabled buttons in UI
        QLayout *buttonLayout = createButtonLayout();
        if (buttonLayout) {
            mainLayout->addLayout(buttonLayout);
        }

        // Don't set minimum size - let it adapt to screen
        // setMinimumSize(m_config.windowWidth, m_config.windowHeight);
    }

    QWidget* createTitleWidget()
    {
        QString titleText = currentTitleText();

        if (m_config.title.layout == "text_only" && titleText.isEmpty()) {
            return nullptr; // No title
        }

        QWidget *titleWidget = new QWidget;
        QHBoxLayout *titleLayout = new QHBoxLayout(titleWidget);
        titleLayout->setContentsMargins(0, 0, 0, 0);

        // Create logo if specified - apply scaling to logo size
        QLabel *logoLabel = nullptr;
        if (!m_config.title.logoPath.isEmpty() && QFile::exists(m_config.title.logoPath)
            && m_config.title.layout != "text_only") {
            logoLabel = new QLabel;
            QPixmap logo(m_config.title.logoPath);
            int scaledLogoWidth = (int)(m_config.title.logoWidth * m_scaleFactor);
            int scaledLogoHeight = (int)(m_config.title.logoHeight * m_scaleFactor);
            logo = logo.scaled(scaledLogoWidth, scaledLogoHeight,
                              Qt::KeepAspectRatio, Qt::SmoothTransformation);
            logoLabel->setPixmap(logo);
            logoLabel->setAlignment(Qt::AlignCenter);
        }

        // Create text label if specified - apply scaling to font size and padding
        QLabel *textLabel = nullptr;
        if (!titleText.isEmpty() && m_config.title.layout != "logo_only") {
            textLabel = new QLabel(titleText);
            textLabel->setAlignment(Qt::AlignCenter);

            // Title font should remain readable - use higher minimum (28px) and less aggressive scaling
            int scaledTitleFontSize = qMax(28, (int)(m_config.title.fontSize * qMax(0.8, m_scaleFactor)));
            int scaledTitlePadding = qMax(10, (int)(15 * m_scaleFactor));
            QString style = QString("QLabel { color: %1; font-size: %2px; font-weight: bold; padding: %3px; }")
                           .arg(m_config.title.color).arg(scaledTitleFontSize).arg(scaledTitlePadding);
            textLabel->setStyleSheet(style);
        }

        // Arrange logo and text based on layout
        if (m_config.title.layout == "logo_left" && logoLabel && textLabel) {
            titleLayout->addWidget(logoLabel);
            titleLayout->addWidget(textLabel);
        } else if (m_config.title.layout == "logo_right" && logoLabel && textLabel) {
            titleLayout->addWidget(textLabel);
            titleLayout->addWidget(logoLabel);
        } else if (m_config.title.layout == "logo_only" && logoLabel) {
            titleLayout->addWidget(logoLabel);
        } else if (textLabel) {
            titleLayout->addWidget(textLabel);
        }

        if (m_config.title.layout == "logo_top" && logoLabel && textLabel) {
            delete titleLayout; // Replace with vertical layout
            QVBoxLayout *vLayout = new QVBoxLayout(titleWidget);
            vLayout->setContentsMargins(0, 0, 0, 0);
            vLayout->addWidget(logoLabel);
            vLayout->addWidget(textLabel);
        }

        return titleWidget;
    }

    QLayout* createButtonLayout()
    {
        QList<ButtonConfig> enabledButtons = buttonsForPage(m_currentPage);

        if (enabledButtons.isEmpty()) {
            return nullptr;
        }

        if (m_config.layout.type == "grid") {
            return createGridLayout(enabledButtons);
        } else if (m_config.layout.type == "horizontal") {
            return createHorizontalLayout(enabledButtons);
        } else {
            return createVerticalLayout(enabledButtons);
        }
    }

    QGridLayout* createGridLayout(const QList<ButtonConfig> &buttons)
    {
        QGridLayout *gridLayout = new QGridLayout;
        gridLayout->setSpacing((int)(m_config.layout.spacing * m_scaleFactor));

        for (const ButtonConfig &config : buttons) {
            QPushButton *button = createButton(config);
            gridLayout->addWidget(button, config.row, config.column,
                                 config.rowSpan, config.columnSpan);
        }

        return gridLayout;
    }

    QVBoxLayout* createVerticalLayout(const QList<ButtonConfig> &buttons)
    {
        QVBoxLayout *vLayout = new QVBoxLayout;
        vLayout->setSpacing((int)(m_config.layout.spacing * m_scaleFactor));

        for (const ButtonConfig &config : buttons) {
            QPushButton *button = createButton(config);
            vLayout->addWidget(button);
        }

        return vLayout;
    }

    QHBoxLayout* createHorizontalLayout(const QList<ButtonConfig> &buttons)
    {
        QHBoxLayout *hLayout = new QHBoxLayout;
        hLayout->setSpacing((int)(m_config.layout.spacing * m_scaleFactor));

        for (const ButtonConfig &config : buttons) {
            QPushButton *button = createButton(config);
            hLayout->addWidget(button);
        }

        return hLayout;
    }

    QPushButton* createButton(const ButtonConfig &config)
    {
        QPushButton *button = new QPushButton;
        button->setProperty("buttonId", config.id);

        // Determine button size: use dynamic if enabled, otherwise use scaled fixed size
        int buttonWidth, buttonHeight;
        bool useDynamic = m_config.layout.dynamicLayout && !m_forceFixedLayout
                          && m_dynamicButtonWidth > 0 && m_dynamicButtonHeight > 0;

        if (useDynamic) {
            buttonWidth = m_dynamicButtonWidth;
            buttonHeight = m_dynamicButtonHeight;
        } else {
            // Fall back to scaled fixed size from config
            buttonWidth = (int)(config.width * m_scaleFactor);
            buttonHeight = (int)(config.height * m_scaleFactor);
        }

        button->setMinimumSize(buttonWidth, buttonHeight);
        button->setMaximumSize(buttonWidth, buttonHeight);

        // Calculate icon scale factor based on button size ratio
        double iconScaleFactor;
        if (useDynamic) {
            // Scale icons proportionally to button size change
            double widthRatio = (double)buttonWidth / config.width;
            double heightRatio = (double)buttonHeight / config.height;
            iconScaleFactor = qMin(widthRatio, heightRatio);
        } else {
            iconScaleFactor = m_scaleFactor;
        }

        int scaledIconWidth = (int)(config.iconWidth * iconScaleFactor);
        int scaledIconHeight = (int)(config.iconHeight * iconScaleFactor);

        // Set icon if available
        if (!config.iconPath.isEmpty() && QFile::exists(config.iconPath)
            && config.iconLayout != "text_only") {
            QPixmap iconPixmap(config.iconPath);
            QPixmap scaledIcon = iconPixmap.scaled(scaledIconWidth, scaledIconHeight,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
            button->setIcon(QIcon(scaledIcon));
            button->setIconSize(QSize(scaledIconWidth, scaledIconHeight));
        }

        // Set text based on layout
        if (config.iconLayout != "icon_only") {
            button->setText(config.text);
        }

        // Apply scaling to font size, border radius, padding, margin
        // Use iconScaleFactor for consistent scaling in dynamic mode
        int scaledFontSize = qMax(12, (int)(config.fontSize * iconScaleFactor));  // Minimum 12px
        int scaledBorderRadius = (int)(config.borderRadius * iconScaleFactor);
        int scaledPadding = qMax(5, (int)(10 * iconScaleFactor));
        int scaledMargin = qMax(2, (int)(5 * iconScaleFactor));
        int scaledBorder = qMax(1, (int)(3 * iconScaleFactor));

        // Set style with scaled values
        QString buttonStyle = QString(
            "QPushButton { "
            "  background-color: %1; "
            "  color: white; "
            "  border: %5px solid #606060; "
            "  border-radius: %2px; "
            "  padding: %6px; "
            "  font-size: %3px; "
            "  font-weight: bold; "
            "  margin: %7px; "
            "}"
            "QPushButton:hover { "
            "  background-color: %4; "
            "  border-color: #808080; "
            "}"
            "QPushButton:pressed { "
            "  background-color: #303030; "
            "  border-color: #404040; "
            "}")
            .arg(config.backgroundColor)
            .arg(scaledBorderRadius)
            .arg(scaledFontSize)
            .arg(config.hoverColor)
            .arg(scaledBorder)
            .arg(scaledPadding)
            .arg(scaledMargin);

        button->setStyleSheet(buttonStyle);

        // Use pressed signal instead of clicked for reliable touch response
        // clicked fires on release which can miss short touches on touchscreens
        connect(button, &QPushButton::pressed, this, &TouchAppLauncher::buttonClicked);

        return button;
    }

    void validatePrograms()
    {
        for (const ButtonConfig &config : m_config.buttons) {
            if (!config.program.isEmpty() && !QFile::exists(config.program)) {
                qWarning() << "Program not found:" << config.program << "for button:" << config.id;
            } else if (!config.program.isEmpty()) {
                qDebug() << "Found program:" << config.program << "for button:" << config.id;
            }
        }

        // Check if Pictures directory exists, create if not
        if (!QDir("/Pictures").exists()) {
            QDir().mkpath("/Pictures");
            qDebug() << "Created /Pictures directory";
        }
    }

    // Add button enable/disable method:
    bool setButtonEnabled(const QString &buttonId, bool enabled)
    {
	for (ButtonConfig &btn : m_config.buttons) {
		if (btn.id == buttonId) {
			if (btn.enabled != enabled) {
				btn.enabled = enabled;
				qDebug() << "Button" << buttonId << (enabled ? "enabled" : "disabled");
				refreshUI();
			}
			return true;
		}
	}
	qWarning() << "Button not found:" << buttonId;
	return false;
    }
    // Add button status query method:
    QString getButtonStatus(const QString &buttonId)
    {
	for (const ButtonConfig &btn : m_config.buttons) {
		if (btn.id == buttonId) {
			return btn.enabled ? "enabled" : "disabled";
		}
	}
	return "ERROR: button-not-found";
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("Touch App Launcher");
    app.setApplicationVersion("2.0");

    // Parse command line arguments
    QString configPath;
    int networkPort = DEFAULT_LAUNCHER_PORT; // Default port
    bool showHelp = false;
    bool forceFixedLayout = false;

    for (int i = 1; i < argc; i++) {
        QString arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            showHelp = true;
            break;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                configPath = argv[i + 1];
                i++; // Skip next argument
            } else {
                qWarning() << "Error: --config requires a file path";
                return 1;
            }
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                bool ok;
                networkPort = QString(argv[i + 1]).toInt(&ok);
                if (!ok || networkPort < 1 || networkPort > 65535) {
                    qWarning() << "Error: --port requires a valid port number (1-65535)";
                    return 1;
                }
                i++; // Skip next argument
            } else {
                qWarning() << "Error: --port requires a port number";
                return 1;
            }
        } else if (arg == "--fixed-layout") {
            forceFixedLayout = true;
        } else if (!arg.startsWith("-")) {
            // Assume it's a config file path if no option specified
            configPath = arg;
        } else {
            qWarning() << "Unknown option:" << arg;
            showHelp = true;
            break;
        }
    }

    if (showHelp) {
        qDebug() << "Touch App Launcher v2.0";
        qDebug() << "";
        qDebug() << "Usage:" << argv[0] << "[OPTIONS] [CONFIG_FILE]";
        qDebug() << "";
        qDebug() << "Options:";
        qDebug() << "  -c, --config FILE    Use specified JSON configuration file";
        qDebug() << "  -p, --port PORT      Network interface port (default:" << DEFAULT_LAUNCHER_PORT << ")";
        qDebug() << "  --fixed-layout       Disable dynamic layout (use scaled fixed sizes from config)";
        qDebug() << "  -h, --help           Show this help message";
        qDebug() << "";
        qDebug() << "Examples:";
        qDebug() << " " << argv[0] << "                              # Use default config and port" << DEFAULT_LAUNCHER_PORT;
        qDebug() << " " << argv[0] << "--port 8090                   # Use port 8090";
        qDebug() << " " << argv[0] << "/tmp/my-launcher.json          # Use specific config file";
        qDebug() << " " << argv[0] << "--config /tmp/my-launcher.json --port 8090 # Use specific config and port";
        qDebug() << "";
        qDebug() << "Config file search order:";
        qDebug() << "  1. Command line argument";
        qDebug() << "  2. /etc/launcher.json";
        qDebug() << "  3. ./launcher.json (current directory)";
        qDebug() << "  4. Built-in defaults";
        qDebug() << "";
        qDebug() << "Network API Commands:";
        qDebug() << "  list-apps                    # List all enabled buttons across all pages";
        qDebug() << "  list-page-apps [page-id]     # List visible buttons on current or specified page";
        qDebug() << "  get-page                     # Get current launcher page";
        qDebug() << "  navigate <page-id>           # Navigate to page";
        qDebug() << "  back                         # Navigate to previous page";
        qDebug() << "  home                         # Navigate to home page";
        qDebug() << "  start-app <app-id>          # Start specific application";
        qDebug() << "  stop-app                    # Stop currently running application";
        qDebug() << "  get-running-app             # Get currently running app or 'none'";
	qDebug() << "  reload-config               # Reload JSON configuration from file";
	qDebug() << "  set-button-enabled <id> <true|false>  # Enable/disable button";
	qDebug() << "  get-button-status <id>      # Get button status (enabled/disabled)";
	qDebug() << "  list-all-buttons            # List all buttons with their status";
	return 0;
    }

    // Create and show launcher
    TouchAppLauncher launcher(configPath, networkPort, forceFixedLayout);
    launcher.showMaximized(); // Full screen for embedded device

    return app.exec();
}

#include "main.moc"
