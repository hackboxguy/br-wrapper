#ifndef VIDEOCONTROLLER_H
#define VIDEOCONTROLLER_H

#include "config.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include "VideoPlayer.h"
#include "NetworkInterface.h"

class VideoController : public QObject
{
    Q_OBJECT

public:
    explicit VideoController(VideoPlayer *player, const QString &videoDir, int networkPort = DEFAULT_VIDEO_PLAYER_PORT, QObject *parent = nullptr);
    ~VideoController();

    Q_INVOKABLE QString getVideoDirectory() const { return m_videoDirectory; }

private slots:
    void handleNetworkCommand(const QString &command);

private:
    VideoPlayer *m_videoPlayer;
    NetworkInterface *m_networkInterface;
    QString m_videoDirectory;
    int m_networkPort;

    QStringList listVideoFiles();
    QString findVideoFile(const QString &filename);
};

#endif // VIDEOCONTROLLER_H
