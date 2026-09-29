#include "FpgaController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>
#include <csignal>
#include <unistd.h>

FpgaController::FpgaController(const Options &options, QObject *parent)
    : QObject(parent), m_options(options)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &FpgaController::progressChanged);
    // Create the log directory as this app's user now: the first sudo'd
    // update-fpga.sh (the probe) would otherwise create it root-owned, and
    // this app could no longer write its own run log beside the script's
    QDir().mkpath(m_options.logDir);
}

FpgaController::~FpgaController()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        // Only a probe or a check can be running here: an update or an
        // activation cannot be left from the UI. Never stop those.
        if (m_mode == Mode::Probe || m_mode == Mode::Check) {
            m_process->terminate();
            if (!m_process->waitForFinished(3000)) m_process->kill();
        } else {
            m_process->waitForFinished(-1);
        }
    }
}

QString FpgaController::displayName() const
{
    if (m_display == "12.3") return QString::fromUtf8("12.3″");
    if (m_display == "14.6-ej") return QString::fromUtf8("14.6″ EJ scan-mode");
    if (m_display == "14.6-direct") return QString::fromUtf8("14.6″ direct-drive");
    return m_display;
}

// 0x1D reg 0x00 is MM DD 00 VV: the release byte, and the release date
QString FpgaController::runningRelease() const
{
    if (m_runRelease.length() != 8) return QString();
    return QString("0x%1 (%2-%3)").arg(m_runRelease.mid(6, 2).toUpper(), m_runRelease.left(2), m_runRelease.mid(2, 2));
}

QString FpgaController::phaseText() const
{
    if (m_phase == "scan") return "Comparing the display's image";
    if (m_phase == "erase") return "Erasing the update slot";
    if (m_phase == "program") return "Writing the new image";
    if (m_phase == "verify") return "Verifying the new image";
    return "Preparing";
}

int FpgaController::elapsedSeconds() const
{
    return m_clock.isValid() ? int(m_clock.elapsed() / 1000) : 0;
}

bool FpgaController::canQuit() const
{
    return m_state != "updating" && m_state != "activating" && m_state != "rebooting";
}

// A dry run never drives the real tool beyond the read-only probe and check
bool FpgaController::realTool() const
{
    return QFileInfo(m_options.tool).fileName() == "update-fpga.sh";
}

// ---- probe and check ------------------------------------------------------------

void FpgaController::probe()
{
    if (m_mode != Mode::None) return;
    setState("probing");
    run(Mode::Probe, {"--probe"});
}

void FpgaController::check()
{
    if (!m_present || m_mode != Mode::None || !canQuit()) return;
    setState("checking");
    run(Mode::Check, {"--check", "--image-dir", m_options.imageDir});
}

// ---- update and activation ---------------------------------------------------------

void FpgaController::startUpdate()
{
    if (m_state != "ready" || m_status != "outdated" || m_mode != Mode::None) return;
    if (m_options.dryRun && realTool()) {
        setOutcome("info", "Dry run: nothing was written",
                   "A dry run does not start update-fpga.sh. Use --fpga-tool with a stand-in to watch the flow.", false);
        setState("failed");
        return;
    }
    holdForUpdate(true);
    QDir().mkpath(m_options.logDir);
    m_log.setFileName(QDir(m_options.logDir).filePath(
        "fpga-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + (m_options.dryRun ? "-dry-run" : "") + ".log"));
    if (!m_log.open(QIODevice::WriteOnly | QIODevice::Text))
        qWarning() << "cannot write FPGA update log" << m_log.fileName();
    logLine(QString("display %1, running %2 build %3 from %4, image %5")
                .arg(m_display, m_runRelease, m_runBuild, m_runSlot, m_image));
    m_phase.clear();
    m_percent = 0;
    m_clock.start();
    m_tick.start();
    setOutcome(QString(), QString(), QString(), false);
    setState("updating");
    emit progressChanged();
    run(Mode::Update, {"--image-dir", m_options.imageDir});
}

void FpgaController::activate()
{
    if (m_state != "written" || m_mode != Mode::None) return;
    if (m_options.dryRun && realTool()) {
        setOutcome("info", "Dry run: the display was not restarted", "", false);
        setState("failed");
        return;
    }
    holdForUpdate(true);
    if (!m_log.isOpen()) {
        m_log.setFileName(QDir(m_options.logDir).filePath(
            "fpga-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-activate.log"));
        m_log.open(QIODevice::WriteOnly | QIODevice::Text);
    }
    setState("activating");
    run(Mode::Activate, {"--activate", "--image-dir", m_options.imageDir});
}

// The lock tells the badge script an update runs; SIGTERM from the launcher
// must not cut an FPGA write or an activation short.
void FpgaController::holdForUpdate(bool on)
{
    if (on) {
        std::signal(SIGTERM, SIG_IGN);
        std::signal(SIGINT, SIG_IGN);
        QFile lock(m_options.lockFile);
        if (lock.open(QIODevice::WriteOnly)) {
            lock.write(QByteArray::number(QCoreApplication::applicationPid()) + "\n");
            lock.close();
        }
    } else {
        QFile lock(m_options.lockFile);
        if (lock.open(QIODevice::ReadOnly)) {
            const qint64 owner = lock.readAll().trimmed().toLongLong();
            lock.close();
            if (owner == QCoreApplication::applicationPid()) QFile::remove(m_options.lockFile);
        }
        std::signal(SIGTERM, SIG_DFL);
        std::signal(SIGINT, SIG_DFL);
    }
}

// ---- the tool --------------------------------------------------------------------

void FpgaController::run(Mode mode, const QStringList &args)
{
    m_mode = mode;
    m_pending.clear();
    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, &FpgaController::onOutput);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &FpgaController::onFinished);
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) onFinished(127, QProcess::CrashExit);
        });
    }
    // update-fpga.sh needs root (I2C, the hh983 poll, systemctl). The launcher
    // runs apps as pi with passwordless sudo; -n fails at once instead of
    // hanging on a prompt. LOG_DIR puts its own log beside this app's.
    QString program = m_options.tool;
    QStringList full = args;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LOG_DIR", m_options.logDir);
    m_process->setProcessEnvironment(env);
    if (!m_options.dryRun && ::geteuid() != 0) {
        full.prepend(program);
        full.prepend("LOG_DIR=" + m_options.logDir);
        full.prepend("-n");
        program = "sudo";
    }
    if (mode == Mode::Update || mode == Mode::Activate)
        logLine(QString("$ %1 %2").arg(program, full.join(' ')));
    m_process->start(program, full);
}

void FpgaController::onOutput()
{
    m_pending += m_process->readAll();
    int nl;
    while ((nl = m_pending.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_pending.left(nl)).trimmed();
        m_pending.remove(0, nl + 1);
        static const QRegularExpression ansi("\\x1B\\[[0-9;]*[A-Za-z]");
        line.remove(ansi);
        if (!line.isEmpty()) handleLine(line);
    }
}

void FpgaController::handleLine(const QString &line)
{
    if (m_mode == Mode::Update || m_mode == Mode::Activate) {
        // PROGRESS arrives once per percent; the log keeps the phases and
        // every 10 % so a cut-off run still shows how far it got
        const bool progress = line.startsWith("PROGRESS ");
        if (!progress) logLine(line);
    }

    if (line.startsWith("PROGRESS ")) {
        QString phase; int pct = m_percent;
        for (const QString &part : line.mid(9).split(' ', Qt::SkipEmptyParts)) {
            if (part.startsWith("phase=")) phase = part.mid(6);
            else if (part.startsWith("percent=")) pct = qBound(0, part.mid(8).toInt(), 100);
        }
        if (phase != m_phase || pct / 10 != m_percent / 10) logLine(QString("%1 %2%").arg(phase).arg(pct));
        m_phase = phase;
        m_percent = pct;
        emit progressChanged();
        return;
    }

    if (!line.startsWith("RESULT ")) return;
    // reason= is free text and always last
    QString head = line.mid(7);
    const int r = head.indexOf(" reason=");
    m_reason = r >= 0 ? head.mid(r + 8) : QString();
    if (r >= 0) head = head.left(r);
    for (const QString &part : head.split(' ', Qt::SkipEmptyParts)) {
        const int eq = part.indexOf('=');
        if (eq <= 0) continue;
        const QString k = part.left(eq), v = part.mid(eq + 1);
        if (k == "display") m_display = v;
        else if (k == "image") m_image = v == "-" ? QString() : v;
        else if (k == "running" && v != "-") {
            const QStringList f = v.split('/');
            m_runRelease = f.value(0) == "-" ? QString() : f.value(0);
            m_runBuild = f.value(1) == "-" ? QString() : f.value(1);
            m_runSlot = f.value(2) == "-" ? QString() : f.value(2);
        } else if (k == "status" && m_mode == Mode::Check) {
            m_status = v;
        }
    }
    emit resultChanged();
}

void FpgaController::onFinished(int exitCode, QProcess::ExitStatus status)
{
    onOutput();
    if (!m_pending.isEmpty()) {
        handleLine(QString::fromUtf8(m_pending).trimmed());
        m_pending.clear();
    }
    const Mode mode = m_mode;
    m_mode = Mode::None;
    const int code = status == QProcess::NormalExit ? exitCode : 128;

    switch (mode) {
    case Mode::Probe: {
        const bool present = code == 0;
        if (present != m_present) {
            m_present = present;
            emit presentChanged();
        } else {
            emit presentChanged();   // the display name may have arrived
        }
        setState(present ? "checking" : "absent");
        if (present) check();
        break;
    }
    case Mode::Check:
        // Exit codes win over the RESULT status for the few that matter
        if (code == 0) m_status = "current";
        else if (code == 10) m_status = "outdated";
        else if (code == 6) m_status = "no-answer";
        else if (code == 2) m_status = "unknown";
        else if (code == 127) { m_status = "unknown"; m_reason = QString("%1 could not be started").arg(m_options.tool); }
        else if (m_status.isEmpty() || m_status == "current" || m_status == "outdated") m_status = "blocked";
        emit resultChanged();
        setState("ready");
        // Automated validation only, never the launcher's button
        if (m_options.autoUpdate && !m_autoUpdateDone && m_status == "outdated") {
            m_autoUpdateDone = true;
            QTimer::singleShot(3000, this, [this]() { startUpdate(); });
        }
        break;
    case Mode::Update:
        finishUpdate(code);
        break;
    case Mode::Activate:
        finishActivate(code);
        break;
    case Mode::None:
        break;
    }
}

void FpgaController::finishUpdate(int code)
{
    m_tick.stop();
    logLine(QString("exit %1").arg(code));
    holdForUpdate(false);
    if (code == 0) {
        // Written and verified; it runs after the FPGA restarts. Until then the
        // launcher shows it, as it does for a board firmware update.
        writeNotice("FPGA restart required");
        setOutcome("success", "FPGA image written",
                   "The new image runs once the display FPGA restarts. Activate it now: the display goes dark "
                   "for a few seconds, then the system restarts.", false);
        setState("written");
        if (m_options.autoActivate && !m_autoActivateDone) {
            m_autoActivateDone = true;
            QTimer::singleShot(5000, this, [this]() { activate(); });
        }
        return;
    }
    if (m_log.isOpen()) m_log.close();
    if (code == 2) {
        setOutcome("warning", "The update did not finish",
                   "Nothing in the display was changed yet. Try again; it continues where it stopped.", true);
    } else if (code == 3) {
        setOutcome("error", "The update must be repeated now",
                   "The display runs its factory image until the update completes. Try again now; it continues "
                   "where it stopped.", true);
    } else if (code == 6) {
        setOutcome("error", "The display stopped answering",
                   "Switch the system off and on, then run the update again. It continues where it stopped.",
                   false, true);
    } else if (code == 1) {
        setOutcome("error", "The update was refused",
                   m_reason.isEmpty() ? QString("Nothing was written.") : m_reason + ". Nothing was written.", false);
    } else if (code == 127) {
        setOutcome("error", "The update tool could not be started",
                   QString("%1 is not available. Nothing was written.").arg(m_options.tool), false);
    } else {
        setOutcome("error", "The update stopped unexpectedly",
                   QString("Check again before doing anything else (exit %1). The log is in %2.")
                       .arg(code).arg(m_options.logDir), true);
    }
    setState("failed");
}

void FpgaController::finishActivate(int code)
{
    logLine(QString("activate exit %1").arg(code));
    if (code == 0) {
        setOutcome("success", "The new FPGA image is running",
                   m_options.dryRun ? "Dry run: a real system would restart now."
                                    : "The system restarts now. If the display stays dark afterwards, "
                                      "switch the system off and on.", false);
        setState("rebooting");
        if (m_log.isOpen()) m_log.close();
        if (m_options.dryRun) {
            holdForUpdate(false);
            return;
        }
        // The user's rule: after an FPGA activation the whole system restarts,
        // so everything downstream of the display comes up fresh
        QTimer::singleShot(4000, this, [this]() {
            QStringList cmd = m_options.rebootCommand.split(' ', Qt::SkipEmptyParts);
            if (cmd.isEmpty()) return;
            const QString program = cmd.takeFirst();
            if (::geteuid() != 0) {
                cmd.prepend(program);
                cmd.prepend("-n");
                QProcess::startDetached("sudo", cmd);
            } else {
                QProcess::startDetached(program, cmd);
            }
        });
        return;
    }
    holdForUpdate(false);
    if (m_log.isOpen()) m_log.close();
    setOutcome("warning", "The display did not restart into the new image",
               "Switch the whole system off and on to finish: the new image is in the display and starts at "
               "power-on.", false, true);
    setState("failed");
}

// ---- small things --------------------------------------------------------------------

void FpgaController::setState(const QString &state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
}

void FpgaController::setOutcome(const QString &kind, const QString &title, const QString &detail,
                                bool canRetry, bool powerCycle)
{
    m_outcomeKind = kind;
    m_outcomeTitle = title;
    m_outcomeDetail = detail;
    m_canRetry = canRetry;
    m_powerCycleRequired = powerCycle;
    emit stateChanged();
}

void FpgaController::writeNotice(const QString &text)
{
    if (m_options.noticeFile.isEmpty()) return;
    QFile f(m_options.noticeFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(text.toUtf8() + "\n");
}

void FpgaController::logLine(const QString &line)
{
    qDebug().noquote() << "[fpga]" << line;
    if (m_log.isOpen()) {
        QTextStream(&m_log) << QDateTime::currentDateTime().toString("HH:mm:ss ") << line << "\n";
        // A run can end in a power cycle; keep every line on disk
        m_log.flush();
        ::fsync(m_log.handle());
    }
}
