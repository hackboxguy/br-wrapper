#ifndef DUALDISPLAYABSOLUTECONTROLLER_H
#define DUALDISPLAYABSOLUTECONTROLLER_H

#include <QObject>
#include <QJsonObject>
#include <QTimer>

class DualDisplayAbsoluteController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double maxNits READ maxNits NOTIFY rangeChanged)
    Q_PROPERTY(double currentNits READ currentNits NOTIFY currentNitsChanged)
    Q_PROPERTY(double nitsStep READ nitsStep CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit DualDisplayAbsoluteController(QObject *parent = nullptr);
    ~DualDisplayAbsoluteController();

    bool active() const { return m_active; }
    bool busy() const { return m_busy; }
    double maxNits() const { return m_maxNits; }
    double currentNits() const { return m_currentNits; }
    double nitsStep() const { return m_nitsStep; }
    QString statusText() const { return m_statusText; }

public slots:
    Q_INVOKABLE void setEnabled(bool enabled, const QString &primaryMode, int primaryBrightness);
    Q_INVOKABLE void setAbsoluteBrightness(double nits);
    Q_INVOKABLE void cleanup();

signals:
    void activeChanged();
    void busyChanged();
    void rangeChanged();
    void currentNitsChanged();
    void statusTextChanged();
    void errorOccurred(const QString &error);

private slots:
    void processBrightnessUpdate();

private:
    bool enableMode(const QString &primaryMode, int primaryBrightness);
    void disableMode();
    bool restorePrimary(QString *error);
    bool runServiceAction(const QString &action, QString *error);
    bool callJson(int port, const QString &command, const QJsonObject &params,
                  QJsonObject *data, QString *error);
    bool setMode(int port, const QString &mode, QString *error);
    bool setRelativeBrightness(int port, int brightness, QString *error);
    bool readMaxBrightness(int port, double *maxNits, QString *error);
    bool sendAbsoluteBrightness(int port, double nits, QString *error);
    bool sendAbsoluteBrightnessToBoth(double nits, QString *error);
    double roundedNits(double nits) const;
    void setEnabledState(bool enabled);
    void setBusy(bool busy);
    void setMaxNits(double maxNits);
    void setCurrentNits(double nits);
    void setStatusText(const QString &text);
    void reportError(const QString &error);

    bool m_active;
    bool m_busy;
    double m_maxNits;
    double m_currentNits;
    double m_pendingNits;
    const double m_nitsStep;
    bool m_brightnessUpdatePending;
    QString m_statusText;
    QString m_restorePrimaryMode;
    int m_restorePrimaryBrightness;
    bool m_hasRestoreState;
    QTimer *m_brightnessThrottle;
};

#endif // DUALDISPLAYABSOLUTECONTROLLER_H
