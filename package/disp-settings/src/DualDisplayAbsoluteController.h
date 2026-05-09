#ifndef DUALDISPLAYABSOLUTECONTROLLER_H
#define DUALDISPLAYABSOLUTECONTROLLER_H

#include <QObject>
#include <QJsonObject>
#include <QByteArray>
#include <QTimer>

class QTcpSocket;

class DualDisplayAbsoluteController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool targetActive READ targetActive NOTIFY targetActiveChanged)
    Q_PROPERTY(bool hardwareAvailable READ hardwareAvailable NOTIFY hardwareAvailableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double maxNits READ maxNits NOTIFY rangeChanged)
    Q_PROPERTY(double currentNits READ currentNits NOTIFY currentNitsChanged)
    Q_PROPERTY(double minNits READ minNits CONSTANT)
    Q_PROPERTY(double nitsStep READ nitsStep CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit DualDisplayAbsoluteController(QObject *parent = nullptr);
    ~DualDisplayAbsoluteController();

    bool active() const { return m_active; }
    bool targetActive() const { return m_targetActive; }
    bool hardwareAvailable() const { return m_hardwareAvailable; }
    bool busy() const { return m_busy; }
    double maxNits() const { return m_maxNits; }
    double currentNits() const { return m_currentNits; }
    double minNits() const { return m_minNits; }
    double nitsStep() const { return m_nitsStep; }
    QString statusText() const { return m_statusText; }

public slots:
    Q_INVOKABLE void setEnabled(bool enabled, const QString &primaryMode, int primaryBrightness);
    Q_INVOKABLE void setAbsoluteBrightness(double nits);
    Q_INVOKABLE void cleanup();

signals:
    void activeChanged();
    void targetActiveChanged();
    void hardwareAvailableChanged();
    void busyChanged();
    void rangeChanged();
    void currentNitsChanged();
    void statusTextChanged();
    void errorOccurred(const QString &error);

private slots:
    void processBrightnessUpdate();
    void adoptSavedState();
    void flushStateSave();
    void refreshHardwareAvailability();
    void onAsyncSocketConnected();
    void onAsyncSocketReadyRead();
    void onAsyncSocketError();
    void onAsyncSocketTimeout();

private:
    bool enableMode(const QString &primaryMode, int primaryBrightness);
    void disableMode();
    bool restorePrimary(QString *error);
    bool runServiceAction(const QString &action, QString *error);
    bool callJson(int port, const QString &command, const QJsonObject &params,
                  QJsonObject *data, QString *error);
    bool setMode(int port, const QString &mode, QString *error);
    bool setRelativeBrightness(int port, int brightness, QString *error);
    bool readStatus(int port, QJsonObject *data, QString *error);
    bool readAbsoluteBrightness(int port, double *nits, QString *error);
    bool readMaxBrightness(int port, double *maxNits, QString *error);
    bool sendAbsoluteBrightness(int port, double nits, QString *error);
    bool sendAbsoluteBrightnessToBoth(double nits, QString *error);
    double roundedNits(double nits) const;
    bool loadStateFile(QString *restoreMode, int *restoreBrightness,
                       double *lastNits, bool *enabled) const;
    bool ensureStateDirectory() const;
    bool saveStateFile() const;
    void scheduleStateSave();
    void clearStateFile() const;
    bool detectHardwareAvailability() const;
    void startAsyncBrightnessSend(double nits);
    void startAsyncBrightnessSendToPort(int port);
    void finishAsyncBrightnessSend();
    void failAsyncBrightnessSend(const QString &error);
    void closeAsyncSocket();
    void setEnabledState(bool enabled);
    void setTargetActive(bool active);
    void setHardwareAvailable(bool available);
    void setBusy(bool busy);
    void setMaxNits(double maxNits);
    void setCurrentNits(double nits);
    void setStatusText(const QString &text);
    void reportError(const QString &error);

    bool m_active;
    bool m_targetActive;
    bool m_hardwareAvailable;
    bool m_busy;
    double m_maxNits;
    double m_currentNits;
    double m_pendingNits;
    const double m_minNits;
    const double m_nitsStep;
    bool m_brightnessUpdatePending;
    QString m_statusText;
    QString m_restorePrimaryMode;
    int m_restorePrimaryBrightness;
    bool m_hasRestoreState;
    QTimer *m_brightnessThrottle;
    QTimer *m_hardwarePollTimer;
    QTimer *m_stateSaveTimer;
    QTimer *m_asyncTimeout;
    QTcpSocket *m_asyncSocket;
    QByteArray m_asyncReadBuffer;
    bool m_sendInFlight;
    bool m_exitFlushed;
    int m_asyncPort;
    double m_inFlightNits;
};

#endif // DUALDISPLAYABSOLUTECONTROLLER_H
