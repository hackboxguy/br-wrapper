#include "GalleryController.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcess>

GalleryController::GalleryController(QObject *parent)
    : QObject(parent)
    , m_picturesDirectory("/Pictures")
    , m_currentIndex(0)
    , m_imageCount(0)
    , m_networkInterface(nullptr)
    , m_usbCopyProcess(nullptr)
    , m_usbCopyBusy(false)
    , m_usbCopyStatus("")
    , m_usbCopyScript("")
{
    // Initialize image list from default directory
    updateImageList();
}

GalleryController::~GalleryController()
{
    if (m_usbCopyProcess) {
        if (m_usbCopyProcess->state() != QProcess::NotRunning) {
            m_usbCopyProcess->terminate();
            if (!m_usbCopyProcess->waitForFinished(1000)) {
                m_usbCopyProcess->kill();
            }
        }
    }

    if (m_networkInterface) {
        delete m_networkInterface;
    }
}

void GalleryController::setPicturesDirectory(const QString &directory)
{
    if (m_picturesDirectory != directory) {
        m_picturesDirectory = directory;
        updateImageList();
        emit picturesDirectoryChanged();
    }
}

void GalleryController::setCurrentIndex(int index)
{
    if (m_currentIndex != index && index >= 0) {
        m_currentIndex = index;
        emit currentIndexChanged();
        qDebug() << "Current index changed to:" << m_currentIndex;
    }
}

void GalleryController::setImageCount(int count)
{
    if (m_imageCount != count) {
        m_imageCount = count;
        emit imageCountChanged();
        qDebug() << "Image count changed to:" << m_imageCount;
    }
}

void GalleryController::setUsbCopyScript(const QString &scriptPath)
{
    if (!scriptPath.isEmpty()) {
        m_usbCopyScript = scriptPath;
    }
}

void GalleryController::setUsbCopyBusy(bool busy)
{
    if (m_usbCopyBusy != busy) {
        m_usbCopyBusy = busy;
        emit usbCopyBusyChanged();
    }
}

void GalleryController::setUsbCopyStatus(const QString &status)
{
    if (m_usbCopyStatus != status) {
        m_usbCopyStatus = status;
        emit usbCopyStatusChanged();
    }
}

bool GalleryController::startNetworkInterface(int port)
{
    m_networkInterface = new NetworkInterface(port, this);
    connect(m_networkInterface, &NetworkInterface::commandReceived,
            this, &GalleryController::handleNetworkCommand);

    return m_networkInterface->startServer();
}

void GalleryController::nextImage()
{
    emit navigateNext();
    qDebug() << "Next image requested";
}

void GalleryController::previousImage()
{
    emit navigatePrevious();
    qDebug() << "Previous image requested";
}

QString GalleryController::getCurrentImage() const
{
    if (m_imageCount > 0 && m_currentIndex >= 0 && m_currentIndex < m_imageList.size()) {
        return m_imageList[m_currentIndex];
    }
    return "";
}

QString GalleryController::listImages() const
{
    return m_imageList.join(",");
}

QString GalleryController::compactProcessOutput(const QString &output) const
{
    QStringList lines;
    for (const QString &line : output.split('\n')) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            lines.append(trimmed);
        }
    }

    if (lines.isEmpty()) {
        return "";
    }

    return lines.join(". ");
}

void GalleryController::copyCurrentImageToUsb()
{
    if (m_usbCopyBusy) {
        setUsbCopyStatus("Copy already running");
        return;
    }

    setUsbCopyStatus("");

    QString currentImage = getCurrentImage();
    if (currentImage.isEmpty()) {
        setUsbCopyStatus("No image selected");
        return;
    }

    QFileInfo imageInfo(currentImage);
    if (!imageInfo.isFile()) {
        setUsbCopyStatus("Image not found");
        return;
    }

    if (m_usbCopyScript.isEmpty()) {
        setUsbCopyStatus("USB copy script not configured");
        return;
    }

    QFileInfo scriptInfo(m_usbCopyScript);
    if (!scriptInfo.isExecutable()) {
        setUsbCopyStatus("USB copy script not found");
        return;
    }

    QProcess *process = new QProcess(this);
    m_usbCopyProcess = process;
    process->setProgram(m_usbCopyScript);
    process->setArguments(QStringList() << currentImage);
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process,
            static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                QString output = compactProcessOutput(QString::fromLocal8Bit(process->readAll()));

                if (m_usbCopyProcess == process) {
                    m_usbCopyProcess = nullptr;
                }

                setUsbCopyBusy(false);
                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    setUsbCopyStatus("Copied to USB. Safe to remove.");
                } else if (!output.isEmpty()) {
                    setUsbCopyStatus(output);
                } else {
                    setUsbCopyStatus("USB copy failed");
                }
                process->deleteLater();
            });

    connect(process,
            &QProcess::errorOccurred,
            this,
            [this, process](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) {
                    return;
                }

                if (m_usbCopyProcess == process) {
                    m_usbCopyProcess = nullptr;
                }

                setUsbCopyBusy(false);
                setUsbCopyStatus("USB copy failed to start");
                process->deleteLater();
            });

    setUsbCopyStatus("Copying...");
    setUsbCopyBusy(true);
    process->start();
}

void GalleryController::updateImageList()
{
    m_imageList.clear();

    QDir dir(m_picturesDirectory);
    if (!dir.exists()) {
        qWarning() << "Pictures directory does not exist:" << m_picturesDirectory;
        setImageCount(0);
        return;
    }

    // Same filters as QML FolderListModel
    QStringList nameFilters;
    nameFilters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.gif" << "*.tiff"
                << "*.JPG" << "*.JPEG" << "*.PNG" << "*.BMP" << "*.GIF" << "*.TIFF";

    QFileInfoList fileList = dir.entryInfoList(nameFilters, QDir::Files, QDir::Name);

    for (const QFileInfo &fileInfo : fileList) {
        m_imageList.append(fileInfo.absoluteFilePath());
    }

    qDebug() << "Found" << m_imageList.size() << "images in" << m_picturesDirectory;

    // Update the count to match the list
    setImageCount(m_imageList.size());
}

QString GalleryController::getImagePathAtIndex(int index) const
{
    if (index >= 0 && index < m_imageList.size()) {
        return m_imageList[index];
    }
    return "";
}

void GalleryController::handleNetworkCommand(const QString &command)
{
    qDebug() << "Network command received:" << command;

    QStringList parts = command.split(' ');
    if (parts.isEmpty()) {
        m_networkInterface->sendResponse("ERROR: Empty command");
        return;
    }

    QString cmd = parts[0].toLower();

    if (cmd == "list-images") {
        // Optional: specify directory as argument
        if (parts.size() >= 2) {
            QString directory = parts.mid(1).join(" "); // Handle spaces in path
            setPicturesDirectory(directory);
        }

        // Return comma-separated list of image filenames (not full paths)
        QStringList filenames;
        for (const QString &fullPath : m_imageList) {
            QFileInfo fileInfo(fullPath);
            filenames.append(fileInfo.fileName());
        }

        if (filenames.isEmpty()) {
            m_networkInterface->sendResponse("");
        } else {
            m_networkInterface->sendResponse(filenames.join(","));
        }
    } else if (cmd == "display" && parts.size() >= 2) {
        QString filePath = parts.mid(1).join(" "); // Handle spaces in filename

        // Check if it's a full path or just a filename
        QFileInfo fileInfo(filePath);
        QString fullPath;

        if (fileInfo.isAbsolute()) {
            fullPath = filePath;
        } else {
            // Relative filename - search in current directory
            fullPath = QDir(m_picturesDirectory).filePath(filePath);
        }

        // Find the image in our list
        int index = m_imageList.indexOf(fullPath);
        if (index >= 0) {
            setCurrentIndex(index);
            emit displayImageRequested(fullPath);
            m_networkInterface->sendResponse("OK");
        } else {
            // Image not in list - try to add it
            if (QFileInfo::exists(fullPath)) {
                m_imageList.append(fullPath);
                setCurrentIndex(m_imageList.size() - 1);
                setImageCount(m_imageList.size());
                emit displayImageRequested(fullPath);
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: Image not found");
            }
        }
    } else if (cmd == "get-image") {
        QString currentImage = getCurrentImage();
        if (currentImage.isEmpty()) {
            m_networkInterface->sendResponse("");
        } else {
            // Return just the filename, not full path
            QFileInfo fileInfo(currentImage);
            m_networkInterface->sendResponse(fileInfo.fileName());
        }
    } else if (cmd == "next") {
        nextImage();
        m_networkInterface->sendResponse("OK");
    } else if (cmd == "prev") {
        previousImage();
        m_networkInterface->sendResponse("OK");
    } else if (cmd == "get-count") {
        m_networkInterface->sendResponse(QString::number(m_imageCount));
    } else if (cmd == "get-index") {
        m_networkInterface->sendResponse(QString::number(m_currentIndex));
    } else if (cmd == "get-directory") {
        m_networkInterface->sendResponse(m_picturesDirectory);
    } else if (cmd == "set-directory" && parts.size() >= 2) {
        QString directory = parts.mid(1).join(" "); // Handle spaces in path
        setPicturesDirectory(directory);
        m_networkInterface->sendResponse("OK");
    } else if (cmd == "copy-current-to-usb") {
        if (m_usbCopyBusy) {
            m_networkInterface->sendResponse("ERROR: Copy already running");
        } else {
            copyCurrentImageToUsb();
            if (m_usbCopyBusy) {
                m_networkInterface->sendResponse("OK: Copy started");
            } else {
                m_networkInterface->sendResponse("ERROR: " + m_usbCopyStatus);
            }
        }
    } else if (cmd == "get-usb-copy-status") {
        m_networkInterface->sendResponse(m_usbCopyStatus);
    } else if (cmd == "quit") {
        m_networkInterface->sendResponse("OK");
        QCoreApplication::quit();
    } else {
        m_networkInterface->sendResponse("ERROR: Unknown command");
    }
}
