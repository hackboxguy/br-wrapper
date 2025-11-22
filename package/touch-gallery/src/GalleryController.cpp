#include "GalleryController.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>

GalleryController::GalleryController(QObject *parent)
    : QObject(parent)
    , m_picturesDirectory("/Pictures")
    , m_currentIndex(0)
    , m_imageCount(0)
    , m_networkInterface(nullptr)
{
    // Initialize image list from default directory
    updateImageList();
}

GalleryController::~GalleryController()
{
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
    } else if (cmd == "quit") {
        m_networkInterface->sendResponse("OK");
        QCoreApplication::quit();
    } else {
        m_networkInterface->sendResponse("ERROR: Unknown command");
    }
}
