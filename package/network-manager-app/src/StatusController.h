#ifndef STATUSCONTROLLER_H
#define STATUSCONTROLLER_H

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class NetTool;

/**
 * StatusController - the Overview section and the header: what every network
 * interface is doing.
 *
 *   net-ctl.sh available      once at start (no NetworkManager: the app says so)
 *   net-ctl.sh status         interfaces + summary; on every change, 5 s fallback
 *   net-ctl.sh monitor        long-running; "NOTICE changed" -> refresh
 *   sudo net-ctl.sh leases    client count of a port in DHCP-server mode
 *
 * The one thing read without the script: byte/packet counters and the carrier
 * from /sys/class/net/<if>/ once a second (plan, section 3, rule 1). A carrier
 * change seen there triggers a refresh at once, so pulling a cable shows
 * within a second, without waiting for NetworkManager's own carrier debounce.
 */
class StatusController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool checked READ checked NOTIFY availabilityChanged)
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY statusChanged)
    Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap rates READ rates NOTIFY ratesChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)

public:
    struct Options {
        QString tool;
        QString sysfs = "/sys/class/net";
        bool dryRun = false;
        int sampleMs = 1000;     // counter sampling (tests sample faster)
        int fallbackMs = 5000;   // refresh without a monitor notice
    };

    explicit StatusController(const Options &options, QObject *parent = nullptr);

    bool checked() const { return m_checked; }
    bool available() const { return m_available; }
    QString unavailableReason() const { return m_unavailableReason; }
    bool loaded() const { return m_loaded; }
    QVariantList interfaces() const { return m_interfaces; }
    QVariantMap summary() const { return m_summary; }
    QVariantMap rates() const { return m_rates; }
    bool dryRun() const { return m_options.dryRun; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap iface(const QString &name) const;

    // Friendly names for the cards: "WiFi", "Ethernet", "USB adapter (ASIX)"
    static QString friendlyName(const QVariantMap &iface);

signals:
    void availabilityChanged();
    void statusChanged();
    void ratesChanged();

private:
    void onStatusResult(const QVariantMap &fields);
    void onStatusFinished(int exitCode);
    void sample();
    void runLeases();

    struct Counters {
        qint64 rx = -1, tx = -1;
        QVariantList rxHistory, txHistory;   // bit/s, the last 60 samples
        int carrier = -1;
    };

    Options m_options;
    NetTool *m_tool = nullptr;      // available, status, leases
    NetTool *m_monitor = nullptr;   // monitor
    QTimer m_fallback;
    QTimer m_debounce;
    QTimer m_sampler;
    QElapsedTimer m_sampleClock;
    bool m_checked = false;
    bool m_available = false;
    QString m_unavailableReason;
    bool m_loaded = false;
    bool m_refreshPending = false;
    bool m_sampleAfter = false;
    QVariantList m_interfaces;
    QVariantList m_collecting;
    QVariantMap m_summary;
    QVariantMap m_collectingSummary;
    QVariantMap m_rates;
    QHash<QString, Counters> m_counters;
    QHash<QString, int> m_clients;      // serving port -> lease count
    QStringList m_leaseQueue;
    int m_monitorRestarts = 0;
};

#endif // STATUSCONTROLLER_H
