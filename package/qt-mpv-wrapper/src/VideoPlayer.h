#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QObject>
#include <QProcess>
#include <QTimer>

class VideoPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)

public:
    explicit VideoPlayer(QObject *parent = nullptr);
    ~VideoPlayer();

    bool isPlaying() const { return m_isPlaying; }

    Q_INVOKABLE void play(const QString &videoPath, bool loop = false);
    Q_INVOKABLE void stop();

signals:
    void isPlayingChanged();
    void playbackFinished();
    void playbackError(const QString &error);
    void readyToHideWindow(); // Signal to hide Qt window before mpv starts

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void startMpvDelayed(); // Actually start mpv after delay

private:
    QProcess *m_process;
    QTimer *m_startTimer;
    bool m_isPlaying;
    QString m_currentVideoPath;
    QString m_pendingVideoPath;
    bool m_pendingLoop;
};

#endif // VIDEOPLAYER_H
