#include "SystemImageController.h"
#include "SystemImageText.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>
#include <csignal>
#include <unistd.h>

namespace {

using SystemImageText::fileValue;

QMap<QString, QString> fields(const QString &line)
{
    QMap<QString, QString> map;
    for (const QString &part : line.split(' ', Qt::SkipEmptyParts)) {
        const int eq = part.indexOf('=');
        if (eq > 0) map.insert(part.left(eq), part.mid(eq + 1));
    }
    return map;
}

QString sizeText(qint64 bytes)
{
    if (bytes >= 1000LL * 1000 * 1000) return QString::number(bytes / 1e9, 'f', 2) + " GB";
    return QString::number(qRound64(bytes / 1e6)) + " MB";
}

struct FailureText { const char *cls; const char *title; const char *detail; int retry; };
// retry: 0 no, 1 yes, 2 once (the first time in this session)
const FailureText kFailures[] = {
    {"source", "No USB stick could be read",
     "Check that the stick is plugged in and formatted FAT32, exFAT or NTFS, then scan again.", 1},
    {"payload", "No bundle, or more than one, on the stick",
     "Leave exactly one .mpupdate file at the top level of the stick, then scan again.", 1},
    {"signature", "The bundle is not signed by this device's release key",
     "This bundle cannot be installed on this device. Nothing was changed.", 0},
    {"compatibility", "The bundle is for a different board or image variant",
     "This bundle cannot be installed on this device. Nothing was changed.", 0},
    {"version", "This version is already running", "Nothing was changed.", 0},
    {"integrity", "The bundle is damaged (checksum mismatch)",
     "Copy the bundle to the stick again, then retry. The running system is unchanged.", 1},
    {"stall", "The write stopped making progress",
     "Check the USB stick, then retry. The running system is unchanged.", 1},
    {"target", "The device could not prepare the other slot",
     "Contact service. The running system is unchanged.", 0},
    {"selector", "The device could not prepare the other slot",
     "Contact service. The running system is unchanged.", 0},
    {"boot", "The device could not prepare the other slot",
     "Contact service. The running system is unchanged.", 0},
    {"image", "The device could not prepare the other slot",
     "Contact service. The running system is unchanged.", 0},
    {"internal", "Unexpected error",
     "The running system is unchanged. See the update log.", 2},
    // Not reachable from the USB path; mapped so a stray class still reads well
    {"network", "The release could not be downloaded", "The running system is unchanged.", 1},
    {"clock", "The system clock is not set", "The running system is unchanged.", 1},
};

} // namespace

SystemImageController::SystemImageController(const Options &options, QObject *parent)
    : QObject(parent), m_options(options)
{
    readRunningImage();
    readStatus();

    m_usbTimer.setInterval(2000);
    connect(&m_usbTimer, &QTimer::timeout, this, &SystemImageController::pollUsb);
    m_progressTimer.setInterval(500);
    connect(&m_progressTimer, &QTimer::timeout, this, &SystemImageController::pollProgress);
    // A candidate commits about 30 s after it is healthy; follow it live
    m_statusTimer.setInterval(5000);
    connect(&m_statusTimer, &QTimer::timeout, this, &SystemImageController::readStatus);
    if (m_lastOutcome == "candidate-armed") m_statusTimer.start();
    m_preflightTimer.setInterval(5000);
    connect(&m_preflightTimer, &QTimer::timeout, this, &SystemImageController::runPreflight);
}

SystemImageController::~SystemImageController()
{
    if (m_scan && m_scan->state() != QProcess::NotRunning) {
        // The scan unmounts on SIGTERM through its trap; give it the chance
        m_scan->terminate();
        if (!m_scan->waitForFinished(3000)) m_scan->kill();
    }
    // An install is never stopped from here: the UI cannot be left while it
    // runs, and the reboot that ends it takes this process with it.
    if (canQuit()) releaseLock();
}

// ---- the running image -------------------------------------------------------

void SystemImageController::readRunningImage()
{
    const QString manifest = m_options.imageManifest;
    const bool haveTool = QFileInfo(m_options.abUpdate).isExecutable();
    m_supported = haveTool && fileValue(manifest, "IMAGE_LAYOUT") == "ab";
    m_runningVersion = fileValue(manifest, "IMAGE_VERSION");
    m_variant = fileValue(manifest, "IMAGE_VARIANT");
    if (m_supported) m_slot = runQuick(m_options.abUpdate, {"--active-slot"}, 3000);
}

void SystemImageController::readStatus()
{
    const QString before = m_lastOutcome;
    m_lastOutcome = fileValue(QDir(m_options.runtimeDir).filePath("status"), "state");
    // The engine's public status carries only state= today; if it ever
    // publishes the candidate's version, that wins over this app's own record
    const QString published = fileValue(QDir(m_options.runtimeDir).filePath("status"), "version");
    m_lastOfferedVersion = !published.isEmpty() ? published : fileValue(m_options.stateFile, "version");
    m_lastFromVersion = fileValue(m_options.stateFile, "from");
    // Why the commit service refused the candidate that fell back (engine
    // 2.06+; only ever published beside state=fallback)
    m_lastRefusedReason = m_lastOutcome == "fallback"
        ? fileValue(QDir(m_options.runtimeDir).filePath("status"), "refused_reason") : QString();
    if (m_lastOutcome != "candidate-armed") m_statusTimer.stop();
    if (m_lastOutcome != before) {
        emit outcomeChanged();
        emit scanChanged();   // canInstall depends on it
    }
    if (m_active) acknowledgeFallback();
}

// The owner's rule: once the section has been seen after a fallback, the badge
// stops repeating "Update rolled back" for that fallback. The record names the
// install it refers to (the same value the badge script derives), so a later
// fallback of another install shows the badge again. The section itself keeps
// showing the outcome line.
void SystemImageController::acknowledgeFallback()
{
    if (m_lastOutcome != "fallback" || m_options.ackFile.isEmpty()) return;
    const QString ref = m_lastOfferedVersion.isEmpty() ? QString("-") : m_lastOfferedVersion;
    if (fileValue(m_options.ackFile, "version") == ref) return;
    QDir().mkpath(QFileInfo(m_options.ackFile).absolutePath());
    QFile f(m_options.ackFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(QString("version=%1\n").arg(ref).toUtf8());
        f.flush();
        ::fsync(f.handle());
    }
}

// Mirrors the engine's commit predicate (ab-update-commit: every unit active,
// NRestarts unchanged from 0) on the running system, as `ab-update install`
// itself warns on stderr - which a UI never sees.
void SystemImageController::runPreflight()
{
    QString units;
    QFile conf(m_options.abConfig);
    if (conf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!conf.atEnd()) {
            const QString line = QString::fromUtf8(conf.readLine()).trimmed();
            if (line.startsWith("AB_HEALTH_UNITS=") && line.length() > 16) units = line.mid(16);
        }
    }
    QStringList problems;
    for (const QString &unit : units.split(' ', Qt::SkipEmptyParts)) {
        const QString active = runQuick(m_options.systemctl, {"is-active", unit}, 2000);
        const QString restarts = runQuick(m_options.systemctl,
                                          {"show", "--value", "--property=NRestarts", unit}, 2000);
        if (active != "active") problems << QString("%1 is not running").arg(unit);
        else if (!restarts.isEmpty() && restarts != "0")
            problems << QString("%1 has restarted %2 times").arg(unit, restarts);
    }
    const QString warning = problems.isEmpty() ? QString()
                          : "The new image would not be kept: " + problems.join("; ");
    if (warning != m_preflightWarning) {
        qInfo().noquote() << "[image] preflight:" << (warning.isEmpty() ? QString("health units ok") : warning);
        m_preflightWarning = warning;
        emit preflightChanged();
    }
}

QString SystemImageController::lastOutcomeText() const
{
    const QString running = m_runningVersion.isEmpty() ? QString("the current image") : m_runningVersion;
    if (m_lastOutcome == "committed")
        return QString("Running %1 (committed)").arg(running);
    if (m_lastOutcome == "fallback") {
        // The version is the engine's (status version=) or, from an older
        // engine, this app's own record of what it started; the reason is the
        // commit service's, when it refused the candidate
        const QString what = (!m_lastOfferedVersion.isEmpty() && m_lastOfferedVersion != m_runningVersion)
                             ? m_lastOfferedVersion : QString("the new image");
        return SystemImageText::fallbackText(what, running, m_lastRefusedReason);
    }
    if (m_lastOutcome == "candidate-armed")
        return QString("The previous update is still being verified; wait a minute and come back");
    return QString();
}

bool SystemImageController::wantsAttention() const
{
    if (!m_supported) return false;
    if (m_lastOutcome == "fallback" || m_lastOutcome == "candidate-armed") return true;
    // The last scan (the launcher badge runs one) found exactly one signed
    // bundle of another version: that is what the badge told the user about
    QFile cache("/run/system-manager/last-scan");
    if (!cache.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    int offers = 0, bundles = 0;
    for (const QString &line : QString::fromUtf8(cache.readAll()).split('\n', Qt::SkipEmptyParts)) {
        if (!line.startsWith("BUNDLE ")) continue;
        ++bundles;
        const auto f = fields(line.mid(7));
        if (f.value("signature") == "ok" && f.value("version") != m_runningVersion) ++offers;
    }
    return bundles == 1 && offers == 1;
}

// ---- helpers -------------------------------------------------------------------

QString SystemImageController::runQuick(const QString &program, const QStringList &args, int timeoutMs) const
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return QString();
    }
    if (p.exitStatus() != QProcess::NormalExit) return QString();
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    // `systemctl is-active` answers "inactive"/"failed" with a non-zero exit;
    // the answer is still the answer. Everything else wants exit 0.
    if (p.exitCode() != 0 && !(args.value(0) == "is-active" && !out.isEmpty())) return QString();
    return out;
}

// Anything that mounts or installs needs root. The launcher runs apps as pi,
// which has passwordless sudo on these images; -n fails at once instead of
// hanging on a prompt nobody can see. A dry run never elevates.
QStringList SystemImageController::sudoWrap(QString &program, QStringList args) const
{
    if (m_options.dryRun || ::geteuid() == 0) return args;
    args.prepend(program);
    args.prepend("-n");
    program = "sudo";
    return args;
}

bool SystemImageController::canInstall() const
{
    return m_supported && m_scanState == "ready" && m_state == "idle"
           && m_lastOutcome != "candidate-armed";
}

void SystemImageController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    emit activeChanged();
    if (!m_supported) return;
    if (m_active && m_state == "idle") {
        m_lastFingerprint = usbFingerprint();
        rescan();
        m_usbTimer.start();
    } else {
        m_usbTimer.stop();
    }
    if (m_active) {
        acknowledgeFallback();
        runPreflight();
        m_preflightTimer.start();
    } else {
        m_preflightTimer.stop();
    }
}

void SystemImageController::setState(const QString &state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
    emit scanChanged();   // canInstall depends on it
}

// ---- USB scan ----------------------------------------------------------------

// Cheap, unprivileged: the block-device inventory the engine reads. A change
// (stick in, stick out, partition appeared) triggers a real scan.
QString SystemImageController::usbFingerprint() const
{
    return runQuick("lsblk", {"-P", "-o", "PATH,TYPE,TRAN,FSTYPE"}, 1500);
}

void SystemImageController::pollUsb()
{
    if (m_state != "idle" || (m_scan && m_scan->state() != QProcess::NotRunning)) return;
    const QString now = usbFingerprint();
    if (now != m_lastFingerprint) {
        m_lastFingerprint = now;
        rescan();
    }
}

void SystemImageController::rescan()
{
    if (!m_supported || m_state == "installing" || m_state == "arming") return;
    if (m_scan && m_scan->state() != QProcess::NotRunning) {
        m_rescanPending = true;
        return;
    }
    if (!m_scan) {
        m_scan = new QProcess(this);
        connect(m_scan, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus) { onScanFinished(code); });
        connect(m_scan, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) onScanFinished(127);
        });
    }
    m_scanState = "scanning";
    emit scanChanged();

    QString program = m_options.scanTool;
    QStringList args;
    if (m_options.dryRun) args << "--dry-run";
    args = sudoWrap(program, args);
    m_scan->start(program, args);
}

void SystemImageController::onScanFinished(int exitCode)
{
    const QString out = QString::fromUtf8(m_scan->readAllStandardOutput());
    if (exitCode != 0 || !out.contains("SUMMARY ")) {
        m_offered.clear();
        m_scanState = "error";
        m_scanDetail = exitCode == 127
            ? QString("The stick scanner could not be started (%1).").arg(m_options.scanTool)
            : QString("The stick could not be scanned (exit %1).").arg(exitCode);
        emit scanChanged();
    } else {
        parseScan(out);
    }
    if (m_rescanPending) {
        m_rescanPending = false;
        rescan();
    }
}

void SystemImageController::parseScan(const QString &out)
{
    QList<QMap<QString, QString>> bundles;
    QString nestedPath;
    QStringList unmountable;
    QMap<QString, QString> summary;
    for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
        if (line.startsWith("BUNDLE ")) bundles << fields(line.mid(7));
        else if (line.startsWith("NESTED ") && nestedPath.isEmpty()) nestedPath = fields(line.mid(7)).value("path");
        else if (line.startsWith("UNMOUNTABLE ")) {
            const auto f = fields(line.mid(12));
            unmountable << QString("%1 (%2)").arg(f.value("device"), f.value("fstype"));
        }
        else if (line.startsWith("SUMMARY ")) summary = fields(line.mid(8));
    }

    m_offered.clear();
    m_scanDetail.clear();
    const int filesystems = summary.value("filesystems").toInt();
    const int sticks = summary.value("sticks").toInt();

    if (summary.value("error") == "needs-root") {
        m_scanState = "error";
        m_scanDetail = "The stick could not be read: the scanner needs root (sudo).";
    } else if (filesystems == 0) {
        m_scanState = "nostick";
        if (sticks > 0) m_scanDetail = "The USB stick has no FAT32, exFAT or NTFS filesystem.";
    } else if (bundles.isEmpty() && !unmountable.isEmpty()) {
        // What the engine does with the same stick: no bundle found and a
        // filesystem it could not mount is a source failure, not "no bundle"
        m_scanState = "unreadable";
        m_scanDetail = QString("%1 could not be mounted read-only. Check the stick, or copy the bundle to "
                               "a FAT32, exFAT or NTFS stick.").arg(unmountable.join(", "));
    } else if (bundles.isEmpty() && summary.value("nested").toInt() > 0) {
        m_scanState = "nested";
        m_scanDetail = QString("%1 is inside a folder. Move it to the top level of the stick.").arg(nestedPath);
    } else if (bundles.isEmpty()) {
        m_scanState = "none";
    } else if (bundles.size() > 1) {
        m_scanState = "many";
    } else {
        const auto &b = bundles.first();
        m_offered["version"] = b.value("version");
        m_offered["variant"] = b.value("variant");
        m_offered["boards"] = b.value("boards");
        m_offered["bytes"] = b.value("bytes").toLongLong();
        m_offered["size"] = sizeText(b.value("bytes").toLongLong());
        m_offered["path"] = b.value("path");
        m_offered["device"] = b.value("device");
        m_offered["signature"] = b.value("signature");
        m_offered["signatureOk"] = b.value("signature") == "ok";
        const QString sig = b.value("signature");
        if (sig != "ok") {
            // The engine checks the same signature first and would refuse
            m_scanState = "one";
            m_scanDetail = sig == "nokey" ? "This device has no release key to check the bundle against."
                         : sig == "unreadable" ? "The bundle could not be read. Copy it to the stick again."
                         : "The bundle is not signed by this device's release key.";
        } else if (b.value("version") == m_runningVersion) {
            m_scanState = "same-version";
        } else {
            m_scanState = "ready";
        }
    }
    if (m_scanState == "ready") runPreflight();
    emit scanChanged();

    // Automated validation only, never the launcher's button: install once
    // per run, a moment after the offer is on screen
    if (m_options.autoInstall && !m_autoInstallDone && canInstall()) {
        m_autoInstallDone = true;
        QTimer::singleShot(3000, this, [this]() { startInstall(); });
    }
}

// ---- install -------------------------------------------------------------------

void SystemImageController::startInstall()
{
    if (!canInstall()) return;

    // A dry run never reaches the real engine: it needs --ab-update pointing at
    // a stand-in (tests/fake-ab-update). The real one would refuse without
    // root anyway; this makes the dry run say so instead of failing.
    const bool realEngine = QFileInfo(m_options.abUpdate).canonicalFilePath()
                            == QFileInfo("/usr/local/bin/ab-update").canonicalFilePath()
                            && QFileInfo::exists("/usr/local/bin/ab-update");
    if (m_options.dryRun && realEngine) {
        m_failureClass.clear();
        m_outcomeTitle = "Dry run: nothing was installed";
        m_outcomeDetail = "A dry run does not start the real installer. Use --ab-update with a stand-in to "
                          "watch the whole flow.";
        m_canRetry = false;
        setState("failed");
        return;
    }

    // The install runs to its end whatever happens to this window; the reboot
    // the engine performs is what ends this process
    std::signal(SIGTERM, SIG_IGN);
    std::signal(SIGINT, SIG_IGN);
    m_usbTimer.stop();

    QFile lock(m_options.lockFile);
    if (lock.open(QIODevice::WriteOnly)) {
        lock.write(QByteArray::number(QCoreApplication::applicationPid()) + "\n");
        lock.close();
    }

    QDir().mkpath(m_options.logDir);
    m_log.setFileName(QDir(m_options.logDir).filePath(
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + (m_options.dryRun ? "-dry-run" : "") + ".log"));
    if (!m_log.open(QIODevice::WriteOnly | QIODevice::Text))
        qWarning() << "cannot write image update log" << m_log.fileName();

    // What this run is about to install, so the line after the reboot can name
    // it. Not for a dry run: its record would name a version a later real
    // fallback never tried to install.
    QDir().mkpath(QFileInfo(m_options.stateFile).absolutePath());
    QFile record(m_options.stateFile);
    if (!m_options.dryRun && record.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        record.write(QString("version=%1\nfrom=%2\nstarted=%3\n")
                         .arg(m_offered.value("version").toString(), m_runningVersion,
                              QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
        record.flush();
        ::fsync(record.handle());
    }

    logLine(QString("offered %1 (%2, %3), %4 at %5 on %6; running %7 slot %8")
                .arg(m_offered.value("version").toString(), m_offered.value("variant").toString(),
                     m_offered.value("boards").toString(), m_offered.value("size").toString(),
                     m_offered.value("path").toString(), m_offered.value("device").toString(),
                     m_runningVersion, m_slot));

    m_failureClass.clear();
    m_outcomeTitle.clear();
    m_outcomeDetail.clear();
    m_canRetry = false;
    m_phase = "starting";
    m_percent = 0;
    m_loggedWriteStep = -1;
    m_pending.clear();
    m_startedAt = QDateTime::currentDateTime();
    m_clock.start();
    setState("installing");
    emit progressChanged();

    if (!m_install) {
        m_install = new QProcess(this);
        m_install->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_install, &QProcess::readyRead, this, &SystemImageController::onInstallOutput);
        connect(m_install, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &SystemImageController::onInstallFinished);
        connect(m_install, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) onInstallFinished(127, QProcess::CrashExit);
        });
    }
    QString program = m_options.abUpdate;
    const QStringList args = sudoWrap(program, {"install", "usb"});
    logLine(QString("$ %1 %2").arg(program, args.join(' ')));
    m_progressTimer.start();
    m_install->start(program, args);
}

// Progress comes from the file the engine publishes, never from its output
void SystemImageController::pollProgress()
{
    const QString path = QDir(m_options.runtimeDir).filePath("progress");
    QFileInfo info(path);
    // The engine removes the previous run's file first; until its new one
    // appears, what is there belongs to an earlier run
    if (!info.exists() || info.lastModified() < m_startedAt.addSecs(-1)) return;

    const QString phase = fileValue(path, "phase");
    const int progress = qBound(0, fileValue(path, "progress").toInt(), 100);
    if (phase.isEmpty()) return;

    int overall = m_percent;
    if (phase == "validating") overall = 1;
    else if (phase == "scanning") overall = 2;
    else if (phase == "fetching") overall = 3;
    else if (phase == "preparing") overall = 4;
    else if (phase == "writing") overall = 5 + progress * 88 / 100;   // the only long phase
    else if (phase == "checking") overall = 94;
    else if (phase == "boot-files") overall = 97;
    else if (phase == "arming") overall = 100;

    if (phase != m_phase || overall != m_percent) {
        if (phase != m_phase) logLine(QString("phase %1 (%2%)").arg(phase).arg(progress));
        // `writing` is the one long phase: a line per 10 % step, so a log cut
        // off by a reset or a power cut shows how far the write had got
        if (phase == "writing" && progress / 10 > m_loggedWriteStep) {
            if (m_loggedWriteStep >= 0 || progress >= 10)
                logLine(QString("writing %1%").arg(progress));
            m_loggedWriteStep = progress / 10;
        }
        m_phase = phase;
        m_percent = overall;
        emit progressChanged();
    }

    if (phase == "arming" && m_state == "installing") {
        // Terminal success: the engine reboots within seconds
        m_progressTimer.stop();
        logLine("armed; the engine reboots into the new image");
        releaseLock();
        setState("arming");
    } else if (phase.startsWith("failed-") && m_state == "installing") {
        fail(phase.mid(7));
    }
}

void SystemImageController::onInstallOutput()
{
    m_pending += m_install->readAll();
    int nl;
    while ((nl = m_pending.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_pending.left(nl)).trimmed();
        m_pending.remove(0, nl + 1);
        static const QRegularExpression ansi("\\x1B\\[[0-9;]*[A-Za-z]");
        line.remove(ansi);
        if (!line.isEmpty()) logLine(line);
    }
}

void SystemImageController::onInstallFinished(int exitCode, QProcess::ExitStatus status)
{
    onInstallOutput();
    if (!m_pending.isEmpty()) {
        logLine(QString::fromUtf8(m_pending).trimmed());
        m_pending.clear();
    }
    logLine(QString("exit %1%2").arg(exitCode).arg(status == QProcess::NormalExit ? "" : " (crashed)"));
    pollProgress();   // the terminal phase may have landed after the last tick

    if (m_state == "installing") {
        m_progressTimer.stop();
        if (exitCode == 0) {
            // Only a stand-in exits 0 without publishing arming; a real one reboots
            setState("arming");
            releaseLock();
        } else {
            // No terminal phase: the engine never got going (sudo refused, a
            // second install running, the tool missing)
            fail(exitCode == 127 ? "missing" : "start");
        }
    }
    if (m_state != "installing" && m_log.isOpen()) m_log.close();
}

void SystemImageController::fail(const QString &cls)
{
    m_progressTimer.stop();
    m_failureClass = cls;
    m_canRetry = false;
    m_outcomeTitle = "The update did not finish";
    m_outcomeDetail = "The running system is unchanged.";

    if (cls == "missing") {
        m_outcomeTitle = "The installer could not be started";
        m_outcomeDetail = QString("%1 is not available. Nothing was changed.").arg(m_options.abUpdate);
    } else if (cls == "start") {
        m_outcomeTitle = "The installer did not start";
        m_outcomeDetail = "Nothing was changed. Another update may be running, or the installer was refused; "
                          "see the update log in " + m_options.logDir + ".";
        m_canRetry = true;
    } else {
        for (const FailureText &f : kFailures) {
            if (cls == QLatin1String(f.cls)) {
                m_outcomeTitle = QString::fromUtf8(f.title);
                m_outcomeDetail = QString::fromUtf8(f.detail);
                m_canRetry = f.retry == 1 || (f.retry == 2 && m_internalRetries == 0);
                if (f.retry == 2) ++m_internalRetries;
            }
        }
    }
    logLine(QString("failed-%1: %2").arg(cls, m_outcomeTitle));
    releaseLock();
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);
    setState("failed");
    if (m_install && m_install->state() == QProcess::NotRunning && m_log.isOpen()) m_log.close();
}

// "Scan again" after a failure: back to the offer, with a fresh look at the stick
void SystemImageController::acknowledgeFailure()
{
    if (m_state != "failed") return;
    setState("idle");
    readStatus();
    rescan();
    if (m_active && m_supported) m_usbTimer.start();
}

// ---- misc ----------------------------------------------------------------------

QString SystemImageController::phaseText() const
{
    if (m_phase == "starting" || m_phase.isEmpty()) return "Starting the installer";
    if (m_phase == "validating") return "Checking the running system";
    if (m_phase == "scanning") return "Reading the USB stick";
    if (m_phase == "fetching") return "Downloading";
    if (m_phase == "preparing") return "Preparing the other slot";
    if (m_phase == "writing") return "Writing the new image";
    if (m_phase == "checking") return "Verifying what was written";
    if (m_phase == "boot-files") return "Installing the boot files";
    if (m_phase == "arming") return "Rebooting into the new image…";
    if (m_phase.startsWith("failed-")) return "Stopped";
    return m_phase;
}

int SystemImageController::elapsedSeconds() const
{
    return m_clock.isValid() ? int(m_clock.elapsed() / 1000) : 0;
}

void SystemImageController::releaseLock()
{
    QFile lock(m_options.lockFile);
    if (!lock.open(QIODevice::ReadOnly)) return;
    const qint64 owner = lock.readAll().trimmed().toLongLong();
    lock.close();
    if (owner == QCoreApplication::applicationPid()) QFile::remove(m_options.lockFile);
}

void SystemImageController::logLine(const QString &line)
{
    qDebug().noquote() << "[image]" << line;
    if (m_log.isOpen()) {
        QTextStream(&m_log) << QDateTime::currentDateTime().toString("HH:mm:ss ") << line << "\n";
        // The run ends in a reboot: keep every line on disk as it is written
        m_log.flush();
        ::fsync(m_log.handle());
    }
}
