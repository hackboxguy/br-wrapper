#ifndef WIFICONTROLLER_H
#define WIFICONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class NetTool;
class StatusController;

/**
 * WifiController - the WiFi section: scan, join, saved networks, the radio.
 *
 *   net-ctl.sh wifi-scan [--rescan]          networks in range + saved profiles
 *   sudo net-ctl.sh wifi-connect --ssid=S [--hidden] [--security=]   password on stdin
 *   sudo net-ctl.sh wifi-disconnect | wifi-forget | wifi-autoconnect | wifi-radio
 *
 * The script keeps the previous connection until the new one is up and
 * restores it on failure (exit 3); this class only shows what happened. The
 * password passes through here once, to the script's stdin; it is not kept,
 * not logged, and not in an argument list. When the network refuses it, the
 * sheet's own field still holds it (passwordRejected()).
 */
class WifiController : public QObject
{
    Q_OBJECT
    // idle | scanning | connecting | working
    Q_PROPERTY(QString busyState READ busyState NOTIFY busyChanged)
    Q_PROPERTY(bool scanned READ scanned NOTIFY networksChanged)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    Q_PROPERTY(QVariantList saved READ saved NOTIFY networksChanged)
    Q_PROPERTY(QString connectingSsid READ connectingSsid NOTIFY busyChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY busyChanged)
    Q_PROPERTY(QString pendingSsid READ pendingSsid NOTIFY busyChanged)
    // the last change's outcome: kind ok|error|info, title, detail, reason, ssid
    Q_PROPERTY(QVariantMap outcome READ outcome NOTIFY outcomeChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)
    // the running change runs as its own systemd unit: leaving the app is safe
    Q_PROPERTY(bool changeDetached READ changeDetached NOTIFY busyChanged)

public:
    struct Options {
        QString tool;
        bool dryRun = false;
    };

    WifiController(const Options &options, StatusController *status, QObject *parent = nullptr);

    QString busyState() const { return m_busyState; }
    bool scanned() const { return m_scanned; }
    QVariantList networks() const { return m_networks; }
    QVariantList saved() const { return m_saved; }
    QString connectingSsid() const { return m_connectingSsid; }
    QString phase() const { return m_phase; }
    QString pendingSsid() const { return m_pendingSsid; }
    QVariantMap outcome() const { return m_outcome; }
    bool dryRun() const { return m_options.dryRun; }
    bool changeDetached() const;

    // The section is on screen: scan now, read the list again every 15 s
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void scan(bool rescan = true);
    // Tapped network: open or saved -> join at once; secured -> passwordNeeded
    Q_INVOKABLE void choose(const QString &ssid);
    Q_INVOKABLE void connectWithPassword(const QString &ssid, const QString &password);
    // security: open | wpa2 | wpa3 (the name is all a hidden network shows)
    Q_INVOKABLE void connectHidden(const QString &ssid, const QString &password, const QString &security);
    Q_INVOKABLE void disconnectWifi();
    Q_INVOKABLE void forget(const QString &ssid);
    Q_INVOKABLE void setAutoconnect(const QString &ssid, bool on);
    Q_INVOKABLE void setRadio(bool on);
    Q_INVOKABLE void clearOutcome();

    // The outcome text for a failed join (also used by tests/offscreen runs)
    static QVariantMap failureOutcome(const QString &reason, const QString &ssid, const QString &restored,
                                      int exitCode);

signals:
    void busyChanged();
    void networksChanged();
    void outcomeChanged();
    void passwordNeeded(const QString &ssid, const QString &security);
    void passwordRejected(const QString &ssid);
    void connected(const QString &ssid);

private:
    enum class Op { None, Scan, Connect, Disconnect, Forget, Autoconnect, Radio };

    void runChange(Op op, const QStringList &args, const QByteArray &secret = QByteArray());
    void connectTo(const QString &ssid, const QString &password, bool hidden, const QString &security);
    void onResult(const QVariantMap &fields);
    void onProgress(const QVariantMap &fields);
    void onFinished(int exitCode);
    void setBusy(const QString &state);
    void setOutcome(const QString &kind, const QString &title, const QString &detail,
                    const QString &reason = QString(), const QString &ssid = QString());
    QVariantMap network(const QString &ssid) const;

    Options m_options;
    StatusController *m_status = nullptr;
    NetTool *m_tool = nullptr;
    Op m_op = Op::None;
    QString m_busyState = "idle";
    bool m_scanned = false;
    bool m_active = false;
    bool m_scanQueued = false;
    QVariantList m_networks, m_saved;
    QVariantList m_collectNetworks, m_collectSaved;
    QString m_connectingSsid;
    QString m_phase;
    QString m_pendingSsid;
    QVariantMap m_lastResult;
    QVariantMap m_outcome;
    QTimer m_rescanTimer;
    std::function<void()> m_queued;   // a change asked for while a scan ran
};

#endif // WIFICONTROLLER_H
