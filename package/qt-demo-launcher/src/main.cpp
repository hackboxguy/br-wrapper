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

class DemoLauncher : public QMainWindow
{
    Q_OBJECT

public:
    DemoLauncher(QWidget *parent = nullptr) : QMainWindow(parent)
    {
        setupUI();
        setupDemoPrograms();
    }

private slots:
    void launchDemo(const QString &program)
    {
        // Set Qt font directory environment variable
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_FONTDIR", "/usr/share/fonts/dejavu/");
        
        QProcess *process = new QProcess(this);
        process->setProcessEnvironment(env);
        
        // Launch the demo program
        QStringList arguments;
        process->start(program, arguments);
        
        if (!process->waitForStarted()) {
            QMessageBox::warning(this, "Error", 
                QString("Failed to start: %1").arg(program));
            delete process;
            return;
        }
        
        // Hide launcher while demo is running
        this->hide();
        
        // Show launcher again when demo exits
        connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                    this->show();
                    process->deleteLater();
                });
    }
    
    void exitLauncher()
    {
        QApplication::quit();
    }

private:
    void setupUI()
    {
        setWindowTitle("Qt Demo Launcher");
        setStyleSheet("QMainWindow { background-color: #2b2b2b; }"
                      "QPushButton { "
                      "  background-color: #4a4a4a; "
                      "  color: white; "
                      "  border: 2px solid #6a6a6a; "
                      "  border-radius: 10px; "
                      "  padding: 15px; "
                      "  font-size: 16px; "
                      "  font-weight: bold; "
                      "}"
                      "QPushButton:hover { "
                      "  background-color: #5a5a5a; "
                      "  border-color: #8a8a8a; "
                      "}"
                      "QPushButton:pressed { "
                      "  background-color: #3a3a3a; "
                      "}"
                      "QLabel { "
                      "  color: white; "
                      "  font-size: 24px; "
                      "  font-weight: bold; "
                      "  padding: 20px; "
                      "}");
        
        QWidget *centralWidget = new QWidget;
        setCentralWidget(centralWidget);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        
        // Title
        QLabel *titleLabel = new QLabel("Qt Demo Programs");
        titleLabel->setAlignment(Qt::AlignCenter);
        mainLayout->addWidget(titleLabel);
        
        // Demo buttons
        QVBoxLayout *buttonLayout = new QVBoxLayout;
        
        QPushButton *fingerPaintBtn = new QPushButton("Finger Paint");
        connect(fingerPaintBtn, &QPushButton::clicked, [this]() {
            launchDemo("/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint");
        });
        buttonLayout->addWidget(fingerPaintBtn);
        
        QPushButton *dialsBtn = new QPushButton("Dials");
        connect(dialsBtn, &QPushButton::clicked, [this]() {
            launchDemo("/usr/lib/qt/examples/widgets/touch/dials/dials");
        });
        buttonLayout->addWidget(dialsBtn);
        
        QPushButton *pinchZoomBtn = new QPushButton("Pinch Zoom");
        connect(pinchZoomBtn, &QPushButton::clicked, [this]() {
            launchDemo("/usr/lib/qt/examples/widgets/touch/pinchzoom/pinchzoom");
        });
        buttonLayout->addWidget(pinchZoomBtn);
        
        QPushButton *scribbleBtn = new QPushButton("Scribble");
        connect(scribbleBtn, &QPushButton::clicked, [this]() {
            launchDemo("/usr/lib/qt/examples/widgets/widgets/scribble/scribble");
        });
        buttonLayout->addWidget(scribbleBtn);
        
        mainLayout->addLayout(buttonLayout);
        
        // Bottom buttons
        QHBoxLayout *bottomLayout = new QHBoxLayout;
        
        QPushButton *exitBtn = new QPushButton("Exit");
        exitBtn->setStyleSheet("QPushButton { background-color: #8b0000; }");
        connect(exitBtn, &QPushButton::clicked, this, &DemoLauncher::exitLauncher);
        bottomLayout->addWidget(exitBtn);
        
        mainLayout->addLayout(bottomLayout);
        
        // Set minimum size for touch interface
        setMinimumSize(480, 320);
    }
    
    void setupDemoPrograms()
    {
        // You can add validation here to check if demo programs exist
        QStringList programs = {
            "/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint",
            "/usr/lib/qt/examples/widgets/touch/dials/dials",
            "/usr/lib/qt/examples/widgets/touch/pinchzoom/pinchzoom",
            "/usr/lib/qt/examples/widgets/widgets/scribble/scribble"
        };
        
        for (const QString &program : programs) {
            if (!QFile::exists(program)) {
                qWarning() << "Demo program not found:" << program;
            }
        }
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application properties
    app.setApplicationName("Qt Demo Launcher");
    app.setApplicationVersion("1.0");
    
    // Create and show launcher
    DemoLauncher launcher;
    launcher.showMaximized(); // Full screen for embedded device
    
    return app.exec();
}

#include "main.moc"
