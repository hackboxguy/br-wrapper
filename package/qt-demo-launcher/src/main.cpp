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

struct ButtonConfig {
    QString id;
    bool enabled = true;
    QString text;
    QString iconPath;
    QString program;
    QStringList arguments;
    QString workingDirectory = "/tmp";
    QString action; // "quit" for special actions

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
    TouchAppLauncher(const QString &configPath = QString(), QWidget *parent = nullptr)
        : QMainWindow(parent), m_configPath(configPath)
    {
        loadConfig();
        setupUI();
        validatePrograms();
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
        if (config.action == "quit") {
            QApplication::quit();
            return;
        }

        // Launch program
        if (!config.program.isEmpty()) {
            QProcessEnvironment env = createTouchEnvironment();
            launchApp(config.program, config.arguments, env, config.workingDirectory);
        }
    }

private:
    LauncherConfig m_config;
    QString m_configPath;

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
            loadDefaultConfig();
            return;
        }

        qDebug() << "Loading configuration from:" << configFile;

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

            if (btn.enabled) {
                m_config.buttons.append(btn);
            }
        }
    }

    void loadDefaultConfig()
    {
        // Fallback to hardcoded configuration
        m_config = LauncherConfig();

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

        // Qt5 framebuffer environment for touch apps
        env.insert("QT_QPA_PLATFORM", "linuxfb");
        env.insert("QT_QPA_FB_HIDECURSOR", "1");
        env.insert("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS", "/dev/input/event0");
        env.insert("QT_QPA_FONTDIR", "/usr/share/fonts/dejavu/");
        env.insert("XDG_RUNTIME_DIR", "/tmp/runtime-root");

        return env;
    }

    void launchApp(const QString &program, const QStringList &args,
                   const QProcessEnvironment &env, const QString &workingDir)
    {
        // Check if program exists
        if (!QFile::exists(program)) {
            QMessageBox::warning(this, "Error",
                QString("Program not found: %1").arg(program));
            return;
        }

        // Create runtime directory if needed
        QDir().mkpath("/tmp/runtime-root");

        QProcess *process = new QProcess(this);
        process->setProcessEnvironment(env);
        process->setWorkingDirectory(workingDir);

        // Launch the program
        process->start(program, args);

        if (!process->waitForStarted(3000)) {
            QMessageBox::warning(this, "Error",
                QString("Failed to start: %1\nError: %2").arg(program, process->errorString()));
            delete process;
            return;
        }

        qDebug() << "Launched:" << program << "with args:" << args;

        // Hide launcher while app is running
        this->hide();

        // Show launcher again when app exits
        connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this, process, program](int exitCode, QProcess::ExitStatus exitStatus) {
                    qDebug() << "App finished:" << program << "Exit code:" << exitCode;
                    this->show();
                    process->deleteLater();
                });
    }

    void setupUI()
    {
        setWindowTitle("Touch App Launcher");
        setStyleSheet("QMainWindow { background-color: #1e1e1e; }");

        QWidget *centralWidget = new QWidget;
        setCentralWidget(centralWidget);

        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setSpacing(20);
        mainLayout->setContentsMargins(m_config.layout.leftMargin, m_config.layout.topMargin,
                                      m_config.layout.rightMargin, m_config.layout.bottomMargin);

        // Create title section
        QWidget *titleWidget = createTitleWidget();
        if (titleWidget) {
            mainLayout->addWidget(titleWidget);
        }

        // Create buttons layout
        QLayout *buttonLayout = createButtonLayout();
        if (buttonLayout) {
            mainLayout->addLayout(buttonLayout);
        }

        // Set window size
        setMinimumSize(m_config.windowWidth, m_config.windowHeight);
    }

    QWidget* createTitleWidget()
    {
        if (m_config.title.layout == "text_only" && m_config.title.text.isEmpty()) {
            return nullptr; // No title
        }

        QWidget *titleWidget = new QWidget;
        QHBoxLayout *titleLayout = new QHBoxLayout(titleWidget);
        titleLayout->setContentsMargins(0, 0, 0, 0);

        // Create logo if specified
        QLabel *logoLabel = nullptr;
        if (!m_config.title.logoPath.isEmpty() && QFile::exists(m_config.title.logoPath)
            && m_config.title.layout != "text_only") {
            logoLabel = new QLabel;
            QPixmap logo(m_config.title.logoPath);
            logo = logo.scaled(m_config.title.logoWidth, m_config.title.logoHeight,
                              Qt::KeepAspectRatio, Qt::SmoothTransformation);
            logoLabel->setPixmap(logo);
            logoLabel->setAlignment(Qt::AlignCenter);
        }

        // Create text label if specified
        QLabel *textLabel = nullptr;
        if (!m_config.title.text.isEmpty() && m_config.title.layout != "logo_only") {
            textLabel = new QLabel(m_config.title.text);
            textLabel->setAlignment(Qt::AlignCenter);

            QString style = QString("QLabel { color: %1; font-size: %2px; font-weight: bold; padding: 15px; }")
                           .arg(m_config.title.color).arg(m_config.title.fontSize);
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
        if (m_config.buttons.isEmpty()) {
            return nullptr;
        }

        if (m_config.layout.type == "grid") {
            return createGridLayout();
        } else if (m_config.layout.type == "horizontal") {
            return createHorizontalLayout();
        } else {
            return createVerticalLayout();
        }
    }

    QGridLayout* createGridLayout()
    {
        QGridLayout *gridLayout = new QGridLayout;
        gridLayout->setSpacing(m_config.layout.spacing);

        for (const ButtonConfig &config : m_config.buttons) {
            QPushButton *button = createButton(config);
            gridLayout->addWidget(button, config.row, config.column,
                                 config.rowSpan, config.columnSpan);
        }

        return gridLayout;
    }

    QVBoxLayout* createVerticalLayout()
    {
        QVBoxLayout *vLayout = new QVBoxLayout;
        vLayout->setSpacing(m_config.layout.spacing);

        for (const ButtonConfig &config : m_config.buttons) {
            QPushButton *button = createButton(config);
            vLayout->addWidget(button);
        }

        return vLayout;
    }

    QHBoxLayout* createHorizontalLayout()
    {
        QHBoxLayout *hLayout = new QHBoxLayout;
        hLayout->setSpacing(m_config.layout.spacing);

        for (const ButtonConfig &config : m_config.buttons) {
            QPushButton *button = createButton(config);
            hLayout->addWidget(button);
        }

        return hLayout;
    }

    QPushButton* createButton(const ButtonConfig &config)
    {
        QPushButton *button = new QPushButton;
        button->setProperty("buttonId", config.id);
        button->setMinimumSize(config.width, config.height);
        button->setMaximumSize(config.width, config.height);

        // Set icon if available
        if (!config.iconPath.isEmpty() && QFile::exists(config.iconPath)
            && config.iconLayout != "text_only") {
            QPixmap iconPixmap(config.iconPath);
            QPixmap scaledIcon = iconPixmap.scaled(config.iconWidth, config.iconHeight,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
            button->setIcon(QIcon(scaledIcon));
            button->setIconSize(QSize(config.iconWidth, config.iconHeight));
        }

        // Set text based on layout
        if (config.iconLayout != "icon_only") {
            button->setText(config.text);
        }

        // Set style
        QString buttonStyle = QString(
            "QPushButton { "
            "  background-color: %1; "
            "  color: white; "
            "  border: 3px solid #606060; "
            "  border-radius: %2px; "
            "  padding: 10px; "
            "  font-size: %3px; "
            "  font-weight: bold; "
            "  margin: 5px; "
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
            .arg(config.borderRadius)
            .arg(config.fontSize)
            .arg(config.hoverColor);

        button->setStyleSheet(buttonStyle);

        connect(button, &QPushButton::clicked, this, &TouchAppLauncher::buttonClicked);

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
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("Touch App Launcher");
    app.setApplicationVersion("2.0");

    // Parse command line arguments
    QString configPath;
    bool showHelp = false;

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
        qDebug() << "  -h, --help           Show this help message";
        qDebug() << "";
        qDebug() << "Examples:";
        qDebug() << " " << argv[0] << "                              # Use default config (/etc/launcher.json)";
        qDebug() << " " << argv[0] << "/tmp/my-launcher.json          # Use specific config file";
        qDebug() << " " << argv[0] << "--config /tmp/my-launcher.json # Use specific config file";
        qDebug() << "";
        qDebug() << "Config file search order:";
        qDebug() << "  1. Command line argument";
        qDebug() << "  2. /etc/launcher.json";
        qDebug() << "  3. ./launcher.json (current directory)";
        qDebug() << "  4. Built-in defaults";
        return 0;
    }

    // Create and show launcher
    TouchAppLauncher launcher(configPath);
    launcher.showMaximized(); // Full screen for embedded device

    return app.exec();
}

#include "main.moc"
