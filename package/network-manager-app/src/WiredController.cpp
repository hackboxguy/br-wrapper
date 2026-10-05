#include "WiredController.h"
#include "NetTool.h"
#include "StatusController.h"

WiredController::WiredController(const Options &options, StatusController *status, QObject *parent)
    : QObject(parent), m_options(options), m_status(status)
{
    m_tool = new NetTool(options.tool, options.dryRun, this);
    connect(m_tool, &NetTool::result, this, &WiredController::onResult);
    connect(m_tool, &NetTool::progress, this, &WiredController::onProgress);
    connect(m_tool, &NetTool::finished, this, &WiredController::onFinished);
    if (m_status) connect(m_status, &StatusController::statusChanged, this, &WiredController::onStatus);

    m_leaseTimer.setInterval(10000);
    connect(&m_leaseTimer, &QTimer::timeout, this, &WiredController::refreshLeases);
}

void WiredController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (active) {
        refreshLeases();
        m_leaseTimer.start();
        // the carrier of serving ports as it is now: a later 0 -> 1 re-probes
        m_carrier.clear();
        onStatus();
    } else {
        m_leaseTimer.stop();
    }
}

void WiredController::setOutcome(const QString &kind, const QString &title, const QString &detail,
                                 const QString &iface)
{
    m_outcome.clear();
    if (!kind.isEmpty()) {
        m_outcome["kind"] = kind;
        m_outcome["title"] = title;
        m_outcome["detail"] = detail;
        m_outcome["iface"] = iface;
    }
    emit outcomeChanged();
}

void WiredController::clearOutcome()
{
    if (!m_outcome.isEmpty()) setOutcome(QString(), QString(), QString(), QString());
}

// One command at a time; the rest wait their turn (a lease refresh must not
// be lost behind a probe, an apply must not be lost behind a lease refresh)
void WiredController::run(Op op, const QString &iface, const QStringList &args)
{
    if (m_tool->busy()) {
        m_queue.append([this, op, iface, args]() { run(op, iface, args); });
        return;
    }
    m_op = op;
    m_opIface = iface;
    m_lastResult.clear();
    m_collectServers.clear();
    m_collectLeases.clear();
    QStringList full = args;
    if (op == Op::Apply && m_options.dryRun) full << "--dry-run";
    m_tool->run(full, true);
}

void WiredController::next()
{
    if (m_tool->busy() || m_queue.isEmpty()) return;
    const std::function<void()> f = m_queue.takeFirst();
    f();
}

void WiredController::probe(const QString &iface)
{
    if (iface.isEmpty()) return;
    QVariantMap p;
    p["state"] = "running";
    m_probes[iface] = p;
    emit probesChanged();
    if (m_busyState == "idle") {
        m_busyState = "probing";
        emit busyChanged();
    }
    run(Op::Probe, iface, {"dhcp-probe", "--iface=" + iface});
}

void WiredController::forgetProbe(const QString &iface)
{
    if (m_probes.remove(iface)) emit probesChanged();
}

void WiredController::apply(const QString &iface, const QString &mode, const QString &ip, const QString &prefix,
                            const QString &gateway, const QString &dns)
{
    if (m_busyState == "applying" || iface.isEmpty()) return;
    QStringList args{"wired-set", "--iface=" + iface, "--mode=" + mode};
    if (mode != "client") {
        args << "--ip=" + ip << "--prefix=" + prefix;
        if (mode == "static") {
            if (!gateway.isEmpty()) args << "--gateway=" + gateway;
            if (!dns.isEmpty()) args << "--dns=" + dns;
        }
    }
    m_busyState = "applying";
    m_applyingIface = iface;
    m_applyMode = mode;
    m_phase.clear();
    clearOutcome();
    emit busyChanged();
    run(Op::Apply, iface, args);
}

void WiredController::refreshLeases()
{
    if (!m_status) return;
    for (const QVariant &v : m_status->interfaces()) {
        const QVariantMap m = v.toMap();
        if (m.value("type") == "ethernet" && m.value("mode") == "server")
            run(Op::Leases, m.value("name").toString(), {"leases", "--iface=" + m.value("name").toString()});
    }
}

// A serving port whose cable just came in: is someone else serving there?
void WiredController::onStatus()
{
    if (!m_status || !m_active) return;
    for (const QVariant &v : m_status->interfaces()) {
        const QVariantMap m = v.toMap();
        const QString name = m.value("name").toString();
        if (m.value("type") != "ethernet" || m.value("mode") != "server") {
            m_carrier.remove(name);
            continue;
        }
        // its lease table, as soon as the port shows up serving
        if (!m_leases.contains(name) && !m_leasesAsked.contains(name)) {
            m_leasesAsked.insert(name);
            run(Op::Leases, name, {"leases", "--iface=" + name});
        }
        const int carrier = m.value("carrier").toInt();
        if (m_carrier.contains(name) && m_carrier.value(name) == 0 && carrier == 1) {
            NetTool::log("carrier came up on serving port " + name + ": probing");
            probe(name);
        }
        m_carrier[name] = carrier;
    }
}

void WiredController::onResult(const QVariantMap &fields)
{
    const QString kind = fields.value("kind").toString();
    if (m_op == Op::Probe && kind == "offer") {
        QVariantMap o;
        o["server"] = fields.value("server");
        o["offered"] = fields.value("offered");
        o["router"] = fields.value("router");
        m_collectServers.append(o);
    } else if (m_op == Op::Probe && kind == "probe") {
        m_lastResult = fields;
    } else if (m_op == Op::Leases && kind == "lease") {
        m_collectLeases.append(fields);
    } else {
        m_lastResult = fields;
    }
}

void WiredController::onProgress(const QVariantMap &fields)
{
    if (m_op != Op::Apply) return;
    m_phase = fields.value("phase").toString();
    emit busyChanged();
}

QVariantMap WiredController::failureOutcome(const QString &reason, const QString &iface, const QString &mode,
                                            const QString &detail, int exitCode)
{
    const QString what = mode == "server" ? "serving addresses" : mode == "static" ? "the fixed address"
                                                                                    : "DHCP client mode";
    QString title = "Could not apply";
    QString text;
    if (reason == "overlap") {
        title = "Address range in use";
        text = detail + ". Choose another subnet.";
    } else if (reason == "bad-arguments") {
        title = "Not a valid setting";
        text = detail + ".";
    } else if (reason == "locked") {
        title = "Not now";
        text = "A system update is running. Try again when it has finished.";
    } else if (reason == "check-failed") {
        text = iface + " came up, but " + what + " did not start: " + detail + ".";
    } else if (reason == "activation-failed") {
        text = iface + " did not come up with " + what + (detail.isEmpty() ? QString(".") : ": " + detail + ".");
    } else {
        text = detail.isEmpty() ? QString("net-ctl.sh failed (exit %1).").arg(exitCode) : detail + ".";
    }
    if (exitCode == 3) text += " The previous settings are back.";
    else if (exitCode == 2) text += " Nothing was changed.";
    QVariantMap m;
    m["kind"] = "error";
    m["title"] = title;
    m["detail"] = text;
    m["iface"] = iface;
    return m;
}

void WiredController::onFinished(int exitCode)
{
    const Op op = m_op;
    const QString iface = m_opIface;
    m_op = Op::None;

    if (op == Op::Probe) {
        QVariantMap p;
        p["state"] = "done";
        p["carrier"] = m_lastResult.value("carrier", "1").toInt();
        p["servers"] = m_collectServers;
        p["failed"] = exitCode != 0;
        m_probes[iface] = p;
        emit probesChanged();
        if (m_busyState == "probing") {
            m_busyState = "idle";
            emit busyChanged();
        }
    } else if (op == Op::Leases) {
        if (exitCode == 0) {
            m_leases[iface] = m_collectLeases;
            emit leasesChanged();
        }
    } else if (op == Op::Apply) {
        const QString mode = m_applyMode;
        m_busyState = "idle";
        m_applyingIface.clear();
        m_phase.clear();
        if (exitCode == 0) {
            const bool pending = m_lastResult.value("pending") == "1";
            const QString ip = m_lastResult.value("ip").toString();
            if (m_options.dryRun) setOutcome("info", "Dry run", "Nothing was changed.", iface);
            else if (pending)
                setOutcome("ok", "Saved", iface + " has no cable: the settings are used when one is plugged in.", iface);
            else
                setOutcome("ok", "Applied", mode == "server" ? iface + " serves addresses as " + ip + "."
                                            : mode == "static" ? iface + " uses the fixed address " + ip + "."
                                            : iface + " is a DHCP client" + (ip.isEmpty() ? "." : " at " + ip + "."),
                           iface);
            forgetProbe(iface);
        } else {
            m_outcome = failureOutcome(m_lastResult.value("reason").toString(), iface, mode,
                                       m_lastResult.value("detail").toString(), exitCode);
            emit outcomeChanged();
        }
        emit busyChanged();
        if (m_status) m_status->refresh();
        if (mode == "server" || exitCode != 0) QTimer::singleShot(1500, this, &WiredController::refreshLeases);
    }
    next();
}
