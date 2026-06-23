#ifndef GALLERYCONTROLLER_H
#define GALLERYCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include "NetworkInterface.h"

class QProcess;

// Default network server port
#define DEFAULT_NETWORK_PORT 8086

class GalleryController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString picturesDirectory READ picturesDirectory WRITE setPicturesDirectory NOTIFY picturesDirectoryChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY imageCountChanged)
    Q_PROPERTY(bool usbCopyBusy READ usbCopyBusy NOTIFY usbCopyBusyChanged)
    Q_PROPERTY(QString usbCopyStatus READ usbCopyStatus NOTIFY usbCopyStatusChanged)

public:
    explicit GalleryController(QObject *parent = nullptr);
    ~GalleryController();

    QString picturesDirectory() const { return m_picturesDirectory; }
    void setPicturesDirectory(const QString &directory);

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);

    int imageCount() const { return m_imageCount; }
    void setImageCount(int count);

    bool usbCopyBusy() const { return m_usbCopyBusy; }
    QString usbCopyStatus() const { return m_usbCopyStatus; }
    void setUsbCopyScript(const QString &scriptPath);

    bool startNetworkInterface(int port);

public slots:
    void nextImage();
    void previousImage();
    QString getCurrentImage() const;
    QString listImages() const;
    Q_INVOKABLE void copyCurrentImageToUsb();
    Q_INVOKABLE void copyImageToUsb(const QString &imagePath);

signals:
    void picturesDirectoryChanged();
    void currentIndexChanged();
    void imageCountChanged();
    void usbCopyBusyChanged();
    void usbCopyStatusChanged();
    void navigateNext();
    void navigatePrevious();
    void displayImageRequested(const QString &filePath);

private slots:
    void handleNetworkCommand(const QString &command);

private:
    QString m_picturesDirectory;
    int m_currentIndex;
    int m_imageCount;
    QStringList m_imageList;
    NetworkInterface *m_networkInterface;
    QProcess *m_usbCopyProcess;
    bool m_usbCopyBusy;
    QString m_usbCopyStatus;
    QString m_usbCopyScript;

    void updateImageList();
    QString getImagePathAtIndex(int index) const;
    void setUsbCopyBusy(bool busy);
    void setUsbCopyStatus(const QString &status);
    QString compactProcessOutput(const QString &output) const;
};

#endif // GALLERYCONTROLLER_H
