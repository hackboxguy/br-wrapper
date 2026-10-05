#ifndef TOOLSCONTROLLER_H
#define TOOLSCONTROLLER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class NetTool;

/**
 * ToolsController - the Tools section: ping, the internet check, iperf3.
 *
 *   net-ctl.sh ping --target= [--iface=] [--count=]
 *   sudo net-ctl.sh internet-check [--iface=]      (root binds each step to the port)
 *   net-ctl.sh iperf-server --start                (runs until stopped)
 *   net-ctl.sh iperf-client --host= --secs= [--udp] [--reverse]
 *
 * One tool at a time: starting one stops the other, and leaving the section
 * or the app stops whatever runs. These are reads - they never detach - and
 * net-ctl.sh stops its ping/iperf3 when its caller is gone, also after a
 * SIGKILL of the app.
 */
class ToolsController : public QObject
{
    Q_OBJECT
    // idle | ping | check | server | client
    Q_PROPERTY(QString running READ running NOTIFY runningChanged)
    Q_PROPERTY(QVariantList replies READ replies NOTIFY pingChanged)       // {seq, ms, lost, reason}
    Q_PROPERTY(QVariantMap pingSummary READ pingSummary NOTIFY pingChanged)
    Q_PROPERTY(QVariantList checkSteps READ checkSteps NOTIFY checkChanged) // {step, state, ms, reason, detail}
    Q_PROPERTY(QVariantMap checkResults READ checkResults NOTIFY checkChanged) // iface -> {ok, ip, gateway}
    Q_PROPERTY(QString checkIface READ checkIface NOTIFY checkChanged)
    Q_PROPERTY(QVariantList samples READ samples NOTIFY iperfChanged)       // Mbit/s per second
    Q_PROPERTY(QVariantMap iperf READ iperf NOTIFY iperfChanged)            // state of the server/client run
    Q_PROPERTY(QString checkName READ checkName CONSTANT)
    Q_PROPERTY(QString checkUrl READ checkUrl CONSTANT)

public:
    struct Options {
        QString tool;
        bool dryRun = false;
        QString checkName = "www.google.com";
        QString checkUrl = "https://www.google.com/generate_204";
    };
    explicit ToolsController(const Options &options, QObject *parent = nullptr);

    QString running() const { return m_running; }
    QVariantList replies() const { return m_replies; }
    QVariantMap pingSummary() const { return m_pingSummary; }
    QVariantList checkSteps() const { return m_checkSteps; }
    QVariantMap checkResults() const { return m_checkResults; }
    QString checkIface() const { return m_checkIface; }
    QVariantList samples() const { return m_samples; }
    QVariantMap iperf() const { return m_iperf; }
    QString checkName() const { return m_options.checkName; }
    QString checkUrl() const { return m_options.checkUrl; }

    Q_INVOKABLE void ping(const QString &target, const QString &iface, int count);
    // iface "" = the default route's port; ip/gateway identify the port's
    // state the result belongs to (the card uses it until they change)
    Q_INVOKABLE void internetCheck(const QString &iface, const QString &ip, const QString &gateway);
    Q_INVOKABLE void startServer();
    Q_INVOKABLE void startClient(const QString &host, int secs, bool udp, bool reverse);
    Q_INVOKABLE void stop();
    // leaving the section stops whatever runs
    Q_INVOKABLE void setActive(bool active);

signals:
    void runningChanged();
    void pingChanged();
    void checkChanged();
    void iperfChanged();

private:
    void start(const QString &what, const QStringList &args, bool elevated);
    void onResult(const QVariantMap &fields);
    void onFinished(int exitCode);

    Options m_options;
    NetTool *m_tool = nullptr;
    QString m_running = "idle";
    QString m_pending;                 // a tool asked for while the last one stops
    QStringList m_pendingArgs;
    bool m_pendingElevated = false;
    QVariantList m_replies;
    QVariantMap m_pingSummary;
    QVariantList m_checkSteps;
    QVariantMap m_checkResults;
    QString m_checkIface;
    QString m_checkIp, m_checkGateway;
    QVariantList m_samples;
    QVariantMap m_iperf;
};

#endif // TOOLSCONTROLLER_H
