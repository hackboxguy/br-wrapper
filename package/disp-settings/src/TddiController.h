#ifndef TDDICONTROLLER_H
#define TDDICONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QString>

/**
 * TddiController - TDDI touch controller information reader
 *
 * Reads information from /proc/android_touch/ filesystem
 */
class TddiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString icType READ icType NOTIFY icTypeChanged)
    Q_PROPERTY(QString fwVersion READ fwVersion NOTIFY fwVersionChanged)
    Q_PROPERTY(QString displayConfig READ displayConfig NOTIFY displayConfigChanged)
    Q_PROPERTY(QString touchConfig READ touchConfig NOTIFY touchConfigChanged)
    Q_PROPERTY(QString customer READ customer NOTIFY customerChanged)
    Q_PROPERTY(QString project READ project NOTIFY projectChanged)
    Q_PROPERTY(QString panelVersion READ panelVersion NOTIFY panelVersionChanged)
    Q_PROPERTY(QString configDate READ configDate NOTIFY configDateChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit TddiController(QObject *parent = nullptr);
    ~TddiController();

    void setBasePath(const QString &path);
    void start();

    QString icType() const { return m_icType; }
    QString fwVersion() const { return m_fwVersion; }
    QString displayConfig() const { return m_displayConfig; }
    QString touchConfig() const { return m_touchConfig; }
    QString customer() const { return m_customer; }
    QString project() const { return m_project; }
    QString panelVersion() const { return m_panelVersion; }
    QString configDate() const { return m_configDate; }
    bool available() const { return m_available; }

public slots:
    void refresh();

signals:
    void icTypeChanged();
    void fwVersionChanged();
    void displayConfigChanged();
    void touchConfigChanged();
    void customerChanged();
    void projectChanged();
    void panelVersionChanged();
    void configDateChanged();
    void availableChanged();
    void errorOccurred(const QString &message);

private:
    QString readFile(const QString &filename);
    void parseVersionInfo(const QString &content);

private:
    QString m_basePath;
    QTimer *m_refreshTimer;

    QString m_icType;
    QString m_fwVersion;
    QString m_displayConfig;
    QString m_touchConfig;
    QString m_customer;
    QString m_project;
    QString m_panelVersion;
    QString m_configDate;
    bool m_available;
};

#endif // TDDICONTROLLER_H
