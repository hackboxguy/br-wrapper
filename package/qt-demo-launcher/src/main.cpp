#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
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

class TouchAppLauncher : public QMainWindow
{
    Q_OBJECT

public:
    TouchAppLauncher(QWidget *parent = nullptr) : QMainWindow(parent)
    {
        setupUI();
        validatePrograms();
    }

private slots:
    void launchFingerPaint()
    {
        QString program = "/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint";
        QProcessEnvironment env = createTouchEnvironment();
        launchApp(program, QStringList(), env);
    }

    void launchScribble()
    {
        QString program = "/usr/lib/qt/examples/widgets/widgets/scribble/scribble";
        QProcessEnvironment env = createTouchEnvironment();
        launchApp(program, QStringList(), env);
    }

    void launchTouchGallery()
    {
        QString program = "/usr/bin/touch-gallery";
        QStringList args;
        args << "/Pictures";  // Pass Pictures directory as argument
        QProcessEnvironment env = createTouchEnvironment();
        launchApp(program, args, env);
    }

    void launchSlideshow()
    {
        QString program = "/usr/bin/touch-gallery";
        QStringList args;
        args << "/Pictures";    // Pictures directory
        args << "slideshow";    // Slideshow mode
        args << "5";           // 5 second interval
        QProcessEnvironment env = createTouchEnvironment();
        launchApp(program, args, env);
    }

    void exitLauncher()
    {
        QApplication::quit();
    }

private:
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

    void launchApp(const QString &program, const QStringList &args, const QProcessEnvironment &env)
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
        process->setWorkingDirectory("/Pictures");  // Set working directory for gallery

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
        setStyleSheet("QMainWindow { background-color: #1e1e1e; }"
                      "QPushButton { "
                      "  background-color: #404040; "
                      "  color: white; "
                      "  border: 3px solid #606060; "
                      "  border-radius: 15px; "
                      "  padding: 25px; "
                      "  font-size: 24px; "
                      "  font-weight: bold; "
                      "  margin: 10px; "
                      "}"
                      "QPushButton:hover { "
                      "  background-color: #505050; "
                      "  border-color: #808080; "
                      "}"
                      "QPushButton:pressed { "
                      "  background-color: #303030; "
                      "  border-color: #404040; "
                      "}"
                      "QPushButton#slideshowBtn { "
                      "  background-color: #2e8b57; "
                      "  border-color: #90ee90; "
                      "}"
                      "QPushButton#slideshowBtn:hover { "
                      "  background-color: #3cb371; "
                      "}"
                      "QPushButton#exitBtn { "
                      "  background-color: #8b0000; "
                      "  border-color: #cd5c5c; "
                      "}"
                      "QPushButton#exitBtn:hover { "
                      "  background-color: #a00000; "
                      "}"
                      "QLabel { "
                      "  color: white; "
                      "  font-size: 32px; "
                      "  font-weight: bold; "
                      "  padding: 30px; "
                      "}");

        QWidget *centralWidget = new QWidget;
        setCentralWidget(centralWidget);

        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setSpacing(20);
        mainLayout->setContentsMargins(50, 50, 50, 50);

        // Title
        QLabel *titleLabel = new QLabel("Touch Applications");
        titleLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(titleLabel);

        // Main app buttons
        QVBoxLayout *buttonLayout = new QVBoxLayout;
        buttonLayout->setSpacing(15);

        // Finger Paint Button
        QPushButton *fingerPaintBtn = new QPushButton("🎨 Finger Paint");
        fingerPaintBtn->setMinimumHeight(100);
        connect(fingerPaintBtn, &QPushButton::clicked, this, &TouchAppLauncher::launchFingerPaint);
        buttonLayout->addWidget(fingerPaintBtn);

        // Scribble Button
        QPushButton *scribbleBtn = new QPushButton("✏️ Scribble");
        scribbleBtn->setMinimumHeight(100);
        connect(scribbleBtn, &QPushButton::clicked, this, &TouchAppLauncher::launchScribble);
        buttonLayout->addWidget(scribbleBtn);

        // Touch Gallery Button
        QPushButton *galleryBtn = new QPushButton("📷 Photo Gallery");
        galleryBtn->setMinimumHeight(100);
        connect(galleryBtn, &QPushButton::clicked, this, &TouchAppLauncher::launchTouchGallery);
        buttonLayout->addWidget(galleryBtn);

        // Slideshow Button (NEW)
        QPushButton *slideshowBtn = new QPushButton("🎞️ Slideshow");
        slideshowBtn->setObjectName("slideshowBtn");
        slideshowBtn->setMinimumHeight(100);
        connect(slideshowBtn, &QPushButton::clicked, this, &TouchAppLauncher::launchSlideshow);
        buttonLayout->addWidget(slideshowBtn);

        mainLayout->addLayout(buttonLayout);

        // Add stretch to push exit button to bottom
        mainLayout->addStretch();

        // Exit button at bottom
        QPushButton *exitBtn = new QPushButton("❌ Exit");
        exitBtn->setObjectName("exitBtn");
        exitBtn->setMinimumHeight(80);
        connect(exitBtn, &QPushButton::clicked, this, &TouchAppLauncher::exitLauncher);
        mainLayout->addWidget(exitBtn);

        // Set minimum size for touch interface (match your 15.6" display)
        setMinimumSize(800, 600);
    }

    void validatePrograms()
    {
        QStringList programs = {
            "/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint",
            "/usr/lib/qt/examples/widgets/widgets/scribble/scribble",
            "/usr/bin/touch-gallery"
        };

        for (const QString &program : programs) {
            if (!QFile::exists(program)) {
                qWarning() << "Program not found:" << program;
            } else {
                qDebug() << "Found program:" << program;
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
    app.setApplicationVersion("1.0");

    // Create and show launcher
    TouchAppLauncher launcher;
    launcher.showMaximized(); // Full screen for embedded device

    return app.exec();
}

#include "main.moc"
