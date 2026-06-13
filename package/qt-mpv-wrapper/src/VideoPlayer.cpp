#include "VideoPlayer.h"
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>

VideoPlayer::VideoPlayer(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_startTimer(new QTimer(this))
    , m_isPlaying(false)
    , m_pendingLoop(false)
{
    // Setup delayed start timer
    m_startTimer->setSingleShot(true);
    m_startTimer->setInterval(500); // 500ms delay to let Qt release framebuffer
    connect(m_startTimer, &QTimer::timeout, this, &VideoPlayer::startMpvDelayed);
}

VideoPlayer::~VideoPlayer()
{
    stop();
}

void VideoPlayer::play(const QString &videoPath, bool loop)
{
    // Stop any existing playback
    stop();

    // Check if file exists
    QFileInfo fileInfo(videoPath);
    if (!fileInfo.exists()) {
        qWarning() << "Video file not found:" << videoPath;
        emit playbackError("File not found: " + videoPath);
        return;
    }

    qDebug() << "Preparing to play video:" << videoPath << "Loop:" << loop;

    // Store pending video info
    m_pendingVideoPath = videoPath;
    m_pendingLoop = loop;

    // Signal QML to hide window first
    emit readyToHideWindow();

    // Start timer to delay mpv launch (gives Qt time to release framebuffer)
    m_startTimer->start();
}

void VideoPlayer::startMpvDelayed()
{
    if (m_pendingVideoPath.isEmpty()) {
        return;
    }

    qDebug() << "Saving playback request and exiting Qt app:" << m_pendingVideoPath << "Loop:" << m_pendingLoop;

    // Save playback request to temporary file
    QFile file("/tmp/qt-mpv-playback.txt");
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << m_pendingVideoPath << "\n";
        out << (m_pendingLoop ? "true" : "false") << "\n";
        file.close();

        qDebug() << "Playback request saved, exiting application...";

        // Exit the Qt application to release framebuffer
        // The wrapper script will launch mpv and then restart us
        QCoreApplication::quit();
    } else {
        qWarning() << "Failed to save playback request to /tmp/qt-mpv-playback.txt";
        emit playbackError("Failed to save playback request");
    }

    // Clear pending
    m_pendingVideoPath.clear();
    m_pendingLoop = false;
}

void VideoPlayer::stop()
{
    if (m_process && m_process->state() == QProcess::Running) {
        qDebug() << "Stopping mpv playback";

        // Try graceful termination first
        m_process->terminate();

        // Wait up to 2 seconds for graceful exit
        if (!m_process->waitForFinished(2000)) {
            qWarning() << "mpv didn't terminate gracefully, killing...";
            m_process->kill();
            m_process->waitForFinished(1000);
        }

        m_process->deleteLater();
        m_process = nullptr;
    }

    if (m_isPlaying) {
        m_isPlaying = false;
        emit isPlayingChanged();
    }

    m_currentVideoPath.clear();
}

void VideoPlayer::onProcessStarted()
{
    qDebug() << "mpv started successfully";
    m_isPlaying = true;
    emit isPlayingChanged();
}

void VideoPlayer::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qDebug() << "mpv finished - Exit code:" << exitCode << "Status:" << exitStatus;

    m_isPlaying = false;
    emit isPlayingChanged();
    emit playbackFinished();

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_currentVideoPath.clear();
}

void VideoPlayer::onProcessError(QProcess::ProcessError error)
{
    QString errorMsg;

    switch (error) {
    case QProcess::FailedToStart:
        errorMsg = "Failed to start mpv. Is it installed?";
        break;
    case QProcess::Crashed:
        errorMsg = "mpv crashed";
        break;
    case QProcess::Timedout:
        errorMsg = "mpv timed out";
        break;
    case QProcess::ReadError:
        errorMsg = "Read error from mpv";
        break;
    case QProcess::WriteError:
        errorMsg = "Write error to mpv";
        break;
    case QProcess::UnknownError:
    default:
        errorMsg = "Unknown error with mpv";
        break;
    }

    qWarning() << "mpv error:" << errorMsg;

    m_isPlaying = false;
    emit isPlayingChanged();
    emit playbackError(errorMsg);

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
}
