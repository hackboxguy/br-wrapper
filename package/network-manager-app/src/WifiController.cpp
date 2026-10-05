#include "WifiController.h"
#include "NetTool.h"
#include "StatusController.h"

static QString quoted(const QString &ssid)
{
    return QString::fromUtf8("“") + ssid + QString::fromUtf8("”");
}

WifiController::WifiController(const Options &options, StatusController *status, QObject *parent)
    : QObject(parent), m_options(options), m_status(status)
{
    m_tool = new NetTool(options.tool, options.dryRun, this);
    connect(m_tool, &NetTool::result, this, &WifiController::onResult);
    connect(m_tool, &NetTool::progress, this, &WifiController::onProgress);
    connect(m_tool, &NetTool::finished, this, &WifiController::onFinished);
    connect(m_tool, &NetTool::notice, this, [this](const QString &text) { if (text == "detached") emit busyChanged(); });

    // While the section is shown: NetworkManager scans on its own; read its
    // list again now and then (no rescan: that is the "Scan again" button)
    m_rescanTimer.setInterval(15000);
    connect(&m_rescanTimer, &QTimer::timeout, this, [this]() { scan(false); });
}

bool WifiController::changeDetached() const
{
    return m_op != Op::None && m_op != Op::Scan && m_tool->detached();
}

void WifiController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (active) {
        scan(true);
        m_rescanTimer.start();
    } else {
        m_rescanTimer.stop();
    }
}

void WifiController::setBusy(const QString &state)
{
    m_busyState = state;
    emit busyChanged();
}

void WifiController::setOutcome(const QString &kind, const QString &title, const QString &detail,
                                const QString &reason, const QString &ssid)
{
    m_outcome.clear();
    if (!kind.isEmpty()) {
        m_outcome["kind"] = kind;
        m_outcome["title"] = title;
        m_outcome["detail"] = detail;
        m_outcome["reason"] = reason;
        m_outcome["ssid"] = ssid;
    }
    emit outcomeChanged();
}

void WifiController::clearOutcome()
{
    if (!m_outcome.isEmpty()) setOutcome(QString(), QString(), QString());
}

QVariantMap WifiController::network(const QString &ssid) const
{
    for (const QVariant &v : m_networks) {
        const QVariantMap m = v.toMap();
        if (m.value("ssid") == ssid) return m;
    }
    return QVariantMap();
}

void WifiController::scan(bool rescan)
{
    if (m_tool->busy()) {
        if (m_op == Op::Scan) return;
        m_scanQueued = true;
        return;
    }
    m_op = Op::Scan;
    m_collectNetworks.clear();
    m_collectSaved.clear();
    setBusy("scanning");
    QStringList args{"wifi-scan"};
    // A rescan needs root (plan 3.1); reading NetworkManager's list does not
    if (rescan) args << "--rescan";
    if (rescan && m_options.dryRun) args << "--dry-run";
    m_tool->run(args, rescan);
}

void WifiController::choose(const QString &ssid)
{
    if (m_tool->busy() && m_op != Op::Scan) return;
    const QVariantMap n = network(ssid);
    const QString security = n.value("security").toString();
    if (security == "enterprise" || security == "other") {
        setOutcome("error", "Not supported",
                   quoted(ssid) + " uses a kind of security this app cannot join. "
                   "Open, WPA2 and WPA3 personal networks are supported.", "unsupported", ssid);
        return;
    }
    if (n.value("active") == "1") return;
    if (security == "open" || n.value("saved") == "1") {
        connectTo(ssid, QString(), false, QString());
        return;
    }
    emit passwordNeeded(ssid, security);
}

void WifiController::connectWithPassword(const QString &ssid, const QString &password)
{
    connectTo(ssid, password, false, QString());
}

void WifiController::connectHidden(const QString &ssid, const QString &password, const QString &security)
{
    connectTo(ssid, password, true, security);
}

void WifiController::connectTo(const QString &ssid, const QString &password, bool hidden, const QString &security)
{
    if (ssid.isEmpty()) return;
    if (m_tool->busy() && m_op != Op::Scan) return;
    QStringList args{"wifi-connect", "--ssid=" + NetTool::percentEncode(ssid)};
    if (hidden) {
        args << "--hidden";
        if (!security.isEmpty()) args << "--security=" + security;
    }
    m_connectingSsid = ssid;
    m_pendingSsid = ssid;
    m_phase.clear();
    clearOutcome();
    // The script reads one line: the password. Empty means "use the saved profile"
    runChange(Op::Connect, args, password.isEmpty() ? QByteArray() : password.toUtf8() + "\n");
}

void WifiController::disconnectWifi()
{
    runChange(Op::Disconnect, {"wifi-disconnect"});
}

void WifiController::forget(const QString &ssid)
{
    m_pendingSsid = ssid;
    runChange(Op::Forget, {"wifi-forget", "--ssid=" + NetTool::percentEncode(ssid)});
}

void WifiController::setAutoconnect(const QString &ssid, bool on)
{
    m_pendingSsid = ssid;
    runChange(Op::Autoconnect, {"wifi-autoconnect", "--ssid=" + NetTool::percentEncode(ssid), on ? "--on" : "--off"});
}

void WifiController::setRadio(bool on)
{
    runChange(Op::Radio, {"wifi-radio", on ? "--on" : "--off"});
}

void WifiController::runChange(Op op, const QStringList &args, const QByteArray &secret)
{
    if (m_tool->busy()) {
        // A scan finishes first (a second or two); the change runs right after
        if (m_op == Op::Scan && !m_queued) {
            m_queued = [this, op, args, secret]() { runChange(op, args, secret); };
            if (op == Op::Connect) setBusy("connecting");
        }
        return;
    }
    m_op = op;
    m_lastResult.clear();
    if (op != Op::Connect) clearOutcome();
    setBusy(op == Op::Connect ? "connecting" : "working");
    QStringList full = args;
    if (m_options.dryRun) full << "--dry-run";
    m_tool->run(full, true, secret);
}

void WifiController::onResult(const QVariantMap &fields)
{
    const QString kind = fields.value("kind").toString();
    if (m_op == Op::Scan) {
        if (kind == "ap") m_collectNetworks.append(fields);
        else if (kind == "saved") m_collectSaved.append(fields);
        return;
    }
    m_lastResult = fields;
}

void WifiController::onProgress(const QVariantMap &fields)
{
    if (m_op != Op::Connect) return;
    m_phase = fields.value("phase").toString();
    emit busyChanged();
}

QVariantMap WifiController::failureOutcome(const QString &reason, const QString &ssid, const QString &restored,
                                           int exitCode)
{
    QString title, detail;
    if (reason == "bad-password") {
        title = "Wrong password";
        detail = "The network " + quoted(ssid) + " did not accept the password.";
    } else if (reason == "not-found") {
        title = "Network not in range";
        detail = quoted(ssid) + " is no longer in range.";
    } else if (reason == "no-address") {
        title = "No address";
        detail = "Connected to " + quoted(ssid) + ", but it gave this rig no address.";
    } else if (reason == "timeout") {
        title = "Timed out";
        detail = quoted(ssid) + " did not answer in time.";
    } else if (reason == "unsupported") {
        title = "Not supported";
        detail = quoted(ssid) + " uses a kind of security this app cannot join.";
    } else if (reason == "radio-off") {
        title = "WiFi is off";
        detail = "Switch WiFi on first.";
    } else if (reason == "locked") {
        title = "Not now";
        detail = "A system update is running. Try again when it has finished.";
    } else {
        title = "Could not connect";
        detail = "Joining " + quoted(ssid) + " failed.";
    }
    if (exitCode == 3 && !restored.isEmpty())
        detail += " The previous connection to " + quoted(restored) + " is back.";
    else if (exitCode == 3)
        detail += " The previous connection is back.";
    else if (exitCode == 2)
        detail += " Nothing was changed.";

    QVariantMap m;
    m["kind"] = "error";
    m["title"] = title;
    m["detail"] = detail;
    m["reason"] = reason;
    m["ssid"] = ssid;
    return m;
}

void WifiController::onFinished(int exitCode)
{
    const Op op = m_op;
    m_op = Op::None;
    // Idle before anything is announced: a handler of the signals below may
    // start the next command at once (a tap queued behind a scan)
    const QString pending = m_pendingSsid;
    m_pendingSsid.clear();
    m_busyState = "idle";

    if (op == Op::Scan) {
        if (exitCode == 0 || !m_collectNetworks.isEmpty() || !m_collectSaved.isEmpty()) {
            m_networks = m_collectNetworks;
            m_saved = m_collectSaved;
        }
        m_scanned = true;
    } else if (op == Op::Connect) {
        const QString ssid = m_connectingSsid;
        const QString reason = m_lastResult.value("reason").toString();
        m_connectingSsid.clear();
        m_phase.clear();
        if (exitCode == 0) {
            setOutcome("ok", "Connected",
                       m_options.dryRun ? QString("Dry run: nothing was changed.")
                                        : "Joined " + quoted(ssid)
                                          + (m_lastResult.value("ip").toString().isEmpty()
                                             ? QString() : " at " + m_lastResult.value("ip").toString()) + ".",
                       QString(), ssid);
            emit connected(ssid);
        } else if (reason == "need-password") {
            // The profile is gone or never was: ask
            emit passwordNeeded(ssid, network(ssid).value("security").toString());
        } else {
            QVariantMap o = failureOutcome(reason, ssid, m_lastResult.value("restored").toString(), exitCode);
            if (exitCode == 127 || exitCode >= 128) o["detail"] = "net-ctl.sh did not finish (exit " + QString::number(exitCode) + ").";
            m_outcome = o;
            emit outcomeChanged();
            if (reason == "bad-password") emit passwordRejected(ssid);
        }
    } else if (op != Op::None) {
        if (exitCode != 0) {
            const QString what = op == Op::Forget ? "Forgetting the network"
                               : op == Op::Autoconnect ? "Changing auto-connect"
                               : op == Op::Radio ? "Switching WiFi"
                               : "Disconnecting";
            QString detail = m_lastResult.value("detail").toString();
            if (m_lastResult.value("reason") == "locked") detail = "A system update is running.";
            setOutcome("error", what + " failed", detail.isEmpty() ? QString("exit %1").arg(exitCode) : detail,
                       m_lastResult.value("reason").toString(), pending);
        } else if (m_options.dryRun) {
            setOutcome("info", "Dry run", "Nothing was changed.");
        }
    }
    emit busyChanged();
    if (op == Op::Scan) emit networksChanged();
    if (m_op != Op::None) return;   // a handler started the next command

    if (m_queued) {
        const std::function<void()> next = m_queued;
        m_queued = nullptr;
        next();
        return;
    }
    if (op != Op::Scan && op != Op::None) {
        if (m_status) m_status->refresh();
        // The radio takes a moment before a scan finds anything
        if (op == Op::Radio) QTimer::singleShot(3000, this, [this]() { scan(true); });
        else scan(false);
    } else if (m_scanQueued) {
        m_scanQueued = false;
        scan(false);
    }
}
