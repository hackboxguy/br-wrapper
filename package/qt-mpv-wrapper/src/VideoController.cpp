#include "VideoController.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>

VideoController::VideoController(VideoPlayer *player, const QString &videoDir, int networkPort, QObject *parent)
    : QObject(parent)
    , m_videoPlayer(player)
    , m_networkInterface(nullptr)
    , m_videoDirectory(videoDir)
    , m_networkPort(networkPort)
{
    // Create network interface
    m_networkInterface = new NetworkInterface(m_networkPort, this);

    // Connect network command signal
    connect(m_networkInterface, &NetworkInterface::commandReceived,
            this, &VideoController::handleNetworkCommand);

    // Start network server
    if (m_networkInterface->startServer()) {
        qDebug() << "Video controller network interface started on port" << m_networkPort;
    } else {
        qWarning() << "Failed to start video controller network interface on port" << m_networkPort;
    }
}

VideoController::~VideoController()
{
}

void VideoController::handleNetworkCommand(const QString &command)
{
    qDebug() << "Network command received:" << command;

    QStringList parts = command.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        m_networkInterface->sendResponse("ERROR: Empty command");
        return;
    }

    QString cmd = parts[0].toLower();

    if (cmd == "list") {
        // List all video files
        QStringList videos = listVideoFiles();
        if (videos.isEmpty()) {
            m_networkInterface->sendResponse("No videos found");
        } else {
            m_networkInterface->sendResponse(videos.join("\n"));
        }

    } else if (cmd == "play" && parts.size() >= 2) {
        // Play specific video: play <filename>
        QString filename = parts.mid(1).join(" "); // Rejoin in case filename has spaces
        QString videoPath = findVideoFile(filename);

        if (videoPath.isEmpty()) {
            m_networkInterface->sendResponse("ERROR: Video not found: " + filename);
        } else {
            m_videoPlayer->play(videoPath, false); // Don't loop by default via network
            m_networkInterface->sendResponse("OK: Playing " + filename);
        }

    } else if (cmd == "play-loop" && parts.size() >= 2) {
        // Play with loop: play-loop <filename>
        QString filename = parts.mid(1).join(" ");
        QString videoPath = findVideoFile(filename);

        if (videoPath.isEmpty()) {
            m_networkInterface->sendResponse("ERROR: Video not found: " + filename);
        } else {
            m_videoPlayer->play(videoPath, true); // Loop enabled
            m_networkInterface->sendResponse("OK: Playing (loop) " + filename);
        }

    } else if (cmd == "stop") {
        // Stop playback
        if (m_videoPlayer->isPlaying()) {
            m_videoPlayer->stop();
            m_networkInterface->sendResponse("OK: Stopped");
        } else {
            m_networkInterface->sendResponse("ERROR: Not playing");
        }

    } else if (cmd == "status" || cmd == "get-status") {
        // Get playback status
        if (m_videoPlayer->isPlaying()) {
            m_networkInterface->sendResponse("playing");
        } else {
            m_networkInterface->sendResponse("stopped");
        }

    } else if (cmd == "help") {
        // Show available commands
        QString helpText = "Available commands:\n"
                          "  list - List all video files\n"
                          "  play <filename> - Play video\n"
                          "  play-loop <filename> - Play video with looping\n"
                          "  stop - Stop playback\n"
                          "  status - Get playback status\n"
                          "  help - Show this help";
        m_networkInterface->sendResponse(helpText);

    } else {
        m_networkInterface->sendResponse("ERROR: Unknown command: " + cmd + " (try 'help')");
    }
}

QStringList VideoController::listVideoFiles()
{
    QStringList nameFilters;
    nameFilters << "*.mp4" << "*.mkv" << "*.avi" << "*.webm" << "*.mov"
                << "*.MP4" << "*.MKV" << "*.AVI" << "*.WEBM" << "*.MOV";

    QDir dir(m_videoDirectory);
    QStringList files = dir.entryList(nameFilters, QDir::Files, QDir::Name);

    return files;
}

QString VideoController::findVideoFile(const QString &filename)
{
    // Try direct path first
    QString fullPath = m_videoDirectory + "/" + filename;
    QFileInfo fileInfo(fullPath);

    if (fileInfo.exists() && fileInfo.isFile()) {
        return fileInfo.absoluteFilePath();
    }

    // Try case-insensitive search
    QStringList videos = listVideoFiles();
    for (const QString &video : videos) {
        if (video.compare(filename, Qt::CaseInsensitive) == 0) {
            return m_videoDirectory + "/" + video;
        }
    }

    return QString(); // Not found
}
