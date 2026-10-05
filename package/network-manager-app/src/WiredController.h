#ifndef WIREDCONTROLLER_H
#define WIREDCONTROLLER_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class NetTool;
class StatusController;

/**
 * WiredController - the Wired section: each wired port's mode, the DHCP
 * probe, the lease table of a serving port.
 *
 *   sudo net-ctl.sh wired-set --iface= --mode=client|static|server ...
 *   sudo net-ctl.sh dhcp-probe --iface=        (net-dhcp-probe.py, ~5 s)
 *   sudo net-ctl.sh leases --iface=            (a serving port, every 10 s while shown)
 *
 * The script keeps the previous settings until the new ones are up and puts
 * them back otherwise (exit 3), detached from the app (rule 6a); this class
 * shows what happened. The ports themselves come from StatusController.
 *
 * Probing: when the section asks for server mode on a port that is not
 * serving yet, and again when a serving port's cable comes in while the
 * section is shown (plan 8: "enabled with no cable, later plugged into the
 * office switch").
 */
class WiredController : public QObject
{
    Q_OBJECT
    // idle | applying | probing
    Q_PROPERTY(QString busyState READ busyState NOTIFY busyChanged)
    Q_PROPERTY(QString applyingIface READ applyingIface NOTIFY busyChanged)
    // client | static | server, or "retry" (the DHCP guard's try again)
    Q_PROPERTY(QString applyingMode READ applyingMode NOTIFY busyChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY busyChanged)
    // iface -> { state: running|done, carrier: 0|1, servers: [{server, offered, router}] }
    Q_PROPERTY(QVariantMap probes READ probes NOTIFY probesChanged)
    // iface -> [{ip, mac, host, expires}]
    Q_PROPERTY(QVariantMap leases READ leases NOTIFY leasesChanged)
    // the last apply: kind ok|error|info, title, detail, iface
    Q_PROPERTY(QVariantMap outcome READ outcome NOTIFY outcomeChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)

public:
    struct Options {
        QString tool;
        bool dryRun = false;
    };

    WiredController(const Options &options, StatusController *status, QObject *parent = nullptr);

    QString busyState() const { return m_busyState; }
    QString applyingIface() const { return m_applyingIface; }
    QString applyingMode() const { return m_applyMode; }
    QString phase() const { return m_phase; }
    QVariantMap probes() const { return m_probes; }
    QVariantMap leases() const { return m_leases; }
    QVariantMap outcome() const { return m_outcome; }
    bool dryRun() const { return m_options.dryRun; }

    // The section is on screen: lease tables refresh, serving ports re-probe on carrier
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void probe(const QString &iface);
    Q_INVOKABLE void forgetProbe(const QString &iface);
    Q_INVOKABLE void apply(const QString &iface, const QString &mode, const QString &ip, const QString &prefix,
                           const QString &gateway, const QString &dns);
    Q_INVOKABLE void clearOutcome();
    // The DHCP guard took a serving port down: bring it up again; the guard
    // probes before it serves (sudo net-ctl.sh dhcp-guard --retry)
    Q_INVOKABLE void guardRetry(const QString &iface);
    // the lease tables of the serving ports, once (Tools: ping a client)
    Q_INVOKABLE void refreshLeases();

    static QVariantMap failureOutcome(const QString &reason, const QString &iface, const QString &mode,
                                      const QString &detail, int exitCode);

signals:
    void busyChanged();
    void probesChanged();
    void leasesChanged();
    void outcomeChanged();

private:
    enum class Op { None, Apply, Probe, Leases, Retry };
    void run(Op op, const QString &iface, const QStringList &args);
    void onResult(const QVariantMap &fields);
    void onProgress(const QVariantMap &fields);
    void onFinished(int exitCode);
    void onStatus();
    void setOutcome(const QString &kind, const QString &title, const QString &detail, const QString &iface);
    void next();

    Options m_options;
    StatusController *m_status = nullptr;
    NetTool *m_tool = nullptr;
    Op m_op = Op::None;
    QString m_opIface;
    QString m_busyState = "idle";
    QString m_applyingIface;
    QString m_applyMode;
    QString m_phase;
    bool m_active = false;
    QVariantMap m_probes, m_leases, m_outcome;
    QVariantList m_collectServers, m_collectLeases;
    QVariantMap m_lastResult;
    QHash<QString, int> m_carrier;            // last carrier seen per serving port
    QSet<QString> m_leasesAsked;              // serving ports whose leases were asked for
    QList<std::function<void()>> m_queue;     // commands waiting for the tool
    QTimer m_leaseTimer;
};

#endif // WIREDCONTROLLER_H
