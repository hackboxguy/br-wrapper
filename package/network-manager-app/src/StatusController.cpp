#include "StatusController.h"
#include "NetTool.h"

#include <QDir>
#include <QFile>

static qint64 readCounter(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    bool ok = false;
    const qint64 v = f.readAll().trimmed().toLongLong(&ok);
    return ok ? v : -1;
}

StatusController::StatusController(const Options &options, QObject *parent)
    : QObject(parent), m_options(options)
{
    m_tool = new NetTool(options.tool, options.dryRun, this);
    m_monitor = new NetTool(options.tool, options.dryRun, this);

    connect(m_tool, &NetTool::result, this, &StatusController::onStatusResult);
    connect(m_tool, &NetTool::finished, this, &StatusController::onStatusFinished);

    // Bursts of NetworkManager events (an activation is a dozen) end in one refresh
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &StatusController::refresh);
    connect(m_monitor, &NetTool::notice, this, [this](const QString &text) {
        if (text == "changed") m_debounce.start();
    });
    // The monitor ends only if NetworkManager went away or the script failed:
    // try again a few times, the fallback timer keeps the page current anyway
    connect(m_monitor, &NetTool::finished, this, [this](int code) {
        if (code == 130 || !m_available || ++m_monitorRestarts > 5) return;
        QTimer::singleShot(2000, this, [this]() { if (!m_monitor->busy()) m_monitor->run({"monitor"}); });
    });

    m_fallback.setInterval(options.fallbackMs);
    connect(&m_fallback, &QTimer::timeout, this, &StatusController::refresh);

    m_sampler.setInterval(options.sampleMs);
    connect(&m_sampler, &QTimer::timeout, this, &StatusController::sample);
}

void StatusController::start()
{
    m_tool->run({"available"});
}

void StatusController::refresh()
{
    if (!m_available) return;
    if (m_tool->busy()) {
        m_refreshPending = true;
        return;
    }
    m_refreshPending = false;
    m_collecting.clear();
    m_collectingSummary.clear();
    m_tool->run({"status"});
}

void StatusController::setLeasesWanted(bool wanted)
{
    if (m_leasesWanted == wanted) return;
    m_leasesWanted = wanted;
    if (wanted) refresh();
}

QVariantMap StatusController::iface(const QString &name) const
{
    for (const QVariant &v : m_interfaces) {
        const QVariantMap m = v.toMap();
        if (m.value("name") == name) return m;
    }
    return QVariantMap();
}

QString StatusController::friendlyName(const QVariantMap &f)
{
    if (f.value("type") == "wifi") return "WiFi";
    if (f.value("usb") != "1") return "Ethernet";
    const QString driver = f.value("driver").toString();
    QString maker;
    if (driver.startsWith("ax88") || driver == "asix") maker = "ASIX";
    else if (driver.startsWith("r815") || driver == "r8152") maker = "Realtek";
    else if (driver.startsWith("lan78") || driver == "smsc95xx" || driver == "smsc75xx") maker = "Microchip";
    else if (driver.startsWith("cdc_")) maker = f.value("product").toString();
    return maker.isEmpty() ? QString("USB adapter") : QString("USB adapter (%1)").arg(maker);
}

void StatusController::onStatusResult(const QVariantMap &fields)
{
    const QString kind = fields.value("kind").toString();
    const QString command = m_tool->command();
    if (command == "available") {
        if (kind == "available") {
            m_available = fields.value("ok") == "1";
            m_unavailableReason = fields.value("reason").toString();
        }
        return;
    }
    if (command == "leases") {
        if (kind == "leases") m_clients.insert(fields.value("iface").toString(), fields.value("count").toInt());
        return;
    }
    if (kind == "iface") {
        QVariantMap f = fields;
        f["friendly"] = friendlyName(fields);
        const QString name = f.value("name").toString();
        if (m_clients.contains(name)) f["clients"] = m_clients.value(name);
        m_collecting.append(f);
    } else if (kind == "summary") {
        m_collectingSummary = fields;
    } else if (kind == "error" && fields.value("reason") == "no-nm") {
        m_available = false;
        m_unavailableReason = fields.value("detail").toString();
        emit availabilityChanged();
    }
}

void StatusController::onStatusFinished(int exitCode)
{
    const QString command = m_tool->command();
    if (command == "available") {
        m_checked = true;
        if (exitCode != 0) {
            m_available = false;
            if (m_unavailableReason.isEmpty())
                m_unavailableReason = exitCode == 127 ? QString("net-ctl.sh could not be started")
                                                      : QString("NetworkManager is not available");
        }
        emit availabilityChanged();
        if (m_available) {
            m_monitor->run({"monitor"});
            m_fallback.start();
            m_sampleClock.start();
            m_sampler.start();
            refresh();
        }
        return;
    }

    if (command == "status") {
        if (exitCode == 0) {
            // Unchanged content is not re-announced: the cards would be rebuilt
            if (m_collecting != m_interfaces || m_collectingSummary != m_summary || !m_loaded) {
                m_interfaces = m_collecting;
                m_summary = m_collectingSummary;
                m_loaded = true;
                emit statusChanged();
            }
            m_leaseQueue.clear();
            QStringList serving;
            for (const QVariant &v : m_interfaces) {
                const QVariantMap m = v.toMap();
                if (m.value("mode") == "server") serving << m.value("name").toString();
            }
            for (auto it = m_clients.begin(); it != m_clients.end();) {
                if (!serving.contains(it.key())) it = m_clients.erase(it); else ++it;
            }
            if (m_leasesWanted) m_leaseQueue = serving;
            m_sampleAfter = true;
        } else if (exitCode == 4) {
            m_available = false;
            emit availabilityChanged();
        }
    } else if (command == "leases") {
        // the count goes into the next status; show it now
        bool changed = false;
        for (int i = 0; i < m_interfaces.size(); ++i) {
            QVariantMap m = m_interfaces[i].toMap();
            const QString name = m.value("name").toString();
            if (m_clients.contains(name) && m.value("clients") != m_clients.value(name)) {
                m["clients"] = m_clients.value(name);
                m_interfaces[i] = m;
                changed = true;
            }
        }
        if (changed) emit statusChanged();
    }

    if (!m_leaseQueue.isEmpty()) runLeases();
    else if (m_refreshPending) refresh();
    // New interfaces get their counters at once; a carrier change seen here
    // asks for a refresh, which waits for the leases if they run
    if (m_sampleAfter) {
        m_sampleAfter = false;
        sample();
    }
}

void StatusController::runLeases()
{
    const QString name = m_leaseQueue.takeFirst();
    // The lease file is root-only (plan 2.1); in a dry run this read fails
    // quietly and the card shows no count
    m_tool->run({"leases", "--iface=" + name}, true);
}

void StatusController::sample()
{
    const qint64 elapsed = m_sampleClock.restart();
    const double seconds = elapsed > 0 ? elapsed / 1000.0 : 1.0;
    bool carrierChanged = false;
    QVariantMap rates;
    for (const QVariant &v : m_interfaces) {
        const QVariantMap m = v.toMap();
        const QString name = m.value("name").toString();
        const QString dir = QDir(m_options.sysfs).filePath(name);
        Counters &c = m_counters[name];
        const qint64 rx = readCounter(dir + "/statistics/rx_bytes");
        const qint64 tx = readCounter(dir + "/statistics/tx_bytes");
        double rxRate = 0, txRate = 0;
        if (c.rx >= 0 && rx >= c.rx) rxRate = (rx - c.rx) * 8 / seconds;
        if (c.tx >= 0 && tx >= c.tx) txRate = (tx - c.tx) * 8 / seconds;
        const bool first = c.rx < 0;
        c.rx = rx; c.tx = tx;
        if (!first) {
            c.rxHistory.append(rxRate);
            c.txHistory.append(txRate);
            while (c.rxHistory.size() > 60) c.rxHistory.removeFirst();
            while (c.txHistory.size() > 60) c.txHistory.removeFirst();
        }

        // Wired ports: the cable, as the kernel sees it now
        if (m.value("type") == "ethernet") {
            const qint64 carrier = readCounter(dir + "/carrier");
            const int now = carrier == 1 ? 1 : 0;
            if (c.carrier >= 0 && now != c.carrier) carrierChanged = true;
            if (c.carrier < 0 && now != m.value("carrier").toInt()) carrierChanged = true;
            c.carrier = now;
        }

        QVariantMap r;
        r["rx"] = rxRate;
        r["tx"] = txRate;
        r["rxHistory"] = c.rxHistory;
        r["txHistory"] = c.txHistory;
        if (c.carrier >= 0) r["carrier"] = c.carrier;
        static const char *const extra[] = {"rx_packets", "tx_packets", "rx_errors", "tx_errors",
                                            "rx_dropped", "tx_dropped", "rx_bytes", "tx_bytes"};
        for (const char *key : extra) r[key] = readCounter(dir + "/statistics/" + key);
        rates[name] = r;
    }
    m_rates = rates;
    emit ratesChanged();
    if (carrierChanged) {
        // NetworkManager follows within a few seconds; ask it now and again
        refresh();
        QTimer::singleShot(1500, this, &StatusController::refresh);
    }
}
