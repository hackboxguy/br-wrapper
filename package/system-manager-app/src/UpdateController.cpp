#include "UpdateController.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>
#include <csignal>
#include <unistd.h>

UpdateController::UpdateController(const Options &options, QObject *parent)
    : QObject(parent), m_options(options)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &UpdateController::activityChanged);
}

UpdateController::~UpdateController()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        // Only reachable if the app is closed mid-check; an update cannot be
        // left from the UI. Never kill an update: let it run to completion.
        if (m_mode == Mode::Check) m_process->kill();
        m_process->waitForFinished(m_mode == Mode::Check ? 2000 : -1);
    }
    QFile::remove(m_options.lockFile);
}

int UpdateController::updatesAvailable() const
{
    int n = 0;
    for (const QVariant &v : m_components) {
        if (v.toMap().value("status").toString() == "outdated") ++n;
    }
    return n;
}

QString UpdateController::summary() const
{
    if (m_state == "checking") return "Checking";
    if (m_state == "updating") return m_options.dryRun ? "Dry run" : "Updating";
    if (m_state == "done") {
        if (m_powerCycleRequired) return "Power cycle required";
        if (m_outcomeKind == "info") return m_options.dryRun ? "Dry run done" : "Up to date";
        return "Needs attention";
    }
    if (!m_checkError.isEmpty()) return "Not checked";
    int n = updatesAvailable();
    if (n == 1) return "1 update available";
    if (n > 1) return QString("%1 updates available").arg(n);
    return "Up to date";
}

int UpdateController::elapsedSeconds() const
{
    return m_clock.isValid() ? int(m_clock.elapsed() / 1000) : 0;
}

void UpdateController::setState(const QString &state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged();
    emit componentsChanged();   // summary depends on the state
}

void UpdateController::check()
{
    if (m_state == "updating") return;
    m_checkError.clear();
    m_components.clear();
    emit componentsChanged();
    setState("checking");
    m_activity = "Reading the firmware each board is running";
    emit activityChanged();
    runTool(Mode::Check, {"--check", "--image-dir", m_options.imageDir});
}

void UpdateController::startUpdate()
{
    if (m_state != "ready" || updatesAvailable() == 0) return;

    // The update runs to completion whatever happens to this window: a stop
    // request from the launcher must not leave the operator without a result.
    std::signal(SIGTERM, SIG_IGN);
    std::signal(SIGINT, SIG_IGN);

    QFile lock(m_options.lockFile);
    if (lock.open(QIODevice::WriteOnly)) {
        lock.write(QByteArray::number(QCoreApplication::applicationPid()) + "\n");
        lock.close();
    }

    QDir().mkpath(m_options.logDir);
    m_log.setFileName(QDir(m_options.logDir).filePath(
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + (m_options.dryRun ? "-dry-run" : "") + ".log"));
    if (!m_log.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "cannot write update log" << m_log.fileName();
    }

    m_boardsUpdated = 0;
    m_powerCycleRequired = false;
    setOutcome(QString(), QString(), QString(), false);
    setState("updating");
    m_activity = m_options.dryRun ? "Dry run: checking the images, nothing will be written"
                                  : "Preparing the update";
    m_clock.start();
    m_tick.start();
    emit activityChanged();

    QStringList args{"--image-dir", m_options.imageDir, "--keep-stream"};
    if (m_options.dryRun) args << "--dry-run";
    runTool(Mode::Update, args);
}

void UpdateController::quitApp()
{
    if (canQuit()) QCoreApplication::quit();
}

void UpdateController::runTool(Mode mode, const QStringList &args)
{
    m_mode = mode;
    m_pending.clear();
    m_resultsSeen = 0;
    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, &UpdateController::onOutput);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &UpdateController::onFinished);
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) onFinished(127, QProcess::CrashExit);
        });
    }

    // The tool needs the I2C device and systemctl, so run it as root. The
    // launcher runs apps as the pi user, which has passwordless sudo on these
    // images; -n makes a missing rule fail at once instead of hanging on a
    // password prompt nobody can see.
    QString program = m_options.tool;
    QStringList fullArgs = args;
    if (::geteuid() != 0) {
        fullArgs.prepend(program);
        fullArgs.prepend("-n");
        program = "sudo";
    }
    logLine(QString("$ %1 %2").arg(program, fullArgs.join(' ')));
    m_process->start(program, fullArgs);
}

void UpdateController::onOutput()
{
    m_pending += m_process->readAll();
    int nl;
    while ((nl = m_pending.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_pending.left(nl)).trimmed();
        m_pending.remove(0, nl + 1);
        if (!line.isEmpty()) handleLine(line);
    }
}

void UpdateController::handleLine(const QString &line)
{
    logLine(line);

    if (line.startsWith("RESULT ")) {
        QMap<QString, QString> fields;
        for (const QString &part : line.mid(7).split(' ', Qt::SkipEmptyParts)) {
            int eq = part.indexOf('=');
            if (eq > 0) fields.insert(part.left(eq), part.mid(eq + 1));
        }
        if (fields.contains("board")) {
            ++m_resultsSeen;
            applyResult(fields);
        }
        return;
    }

    if (m_mode != Mode::Update) return;

    // Progress for the operator: the script's own log lines, and disptool's
    // phase lines, without the tags.
    QString text = line;
    text.remove(QRegularExpression("^\\[update-iocs\\]\\s*"));
    if (text.startsWith("  ") || text.length() < 4) return;
    m_activity = text;
    emit activityChanged();
}

void UpdateController::applyResult(const QMap<QString, QString> &fields)
{
    QVariantMap entry = describe(fields, m_mode == Mode::Update);
    const QString board = entry.value("board").toString();

    if (m_mode == Mode::Update && fields.value("status") == "ok") ++m_boardsUpdated;

    for (int i = 0; i < m_components.size(); ++i) {
        QVariantMap existing = m_components[i].toMap();
        if (existing.value("board") == board) {
            // An update result keeps what the check knew (the shipped version)
            if (entry.value("shipped").toString() == QString::fromUtf8("—"))
                entry["shipped"] = existing.value("shipped");
            m_components[i] = entry;
            emit componentsChanged();
            return;
        }
    }
    m_components.append(entry);
    emit componentsChanged();
}

void UpdateController::onFinished(int exitCode, QProcess::ExitStatus status)
{
    onOutput();
    if (!m_pending.isEmpty()) {
        handleLine(QString::fromUtf8(m_pending).trimmed());
        m_pending.clear();
    }
    const Mode mode = m_mode;
    m_mode = Mode::None;
    if (mode == Mode::Check) finishCheck(exitCode);
    else if (mode == Mode::Update) finishUpdate(exitCode, status != QProcess::NormalExit);
}

void UpdateController::finishCheck(int exitCode)
{
    if (m_resultsSeen == 0) {
        m_checkError = (exitCode == 127)
            ? QString("The update tool could not be started (%1).").arg(m_options.tool)
            : QString("The update tool gave no result (exit %1).").arg(exitCode);
    }
    setState("ready");
    emit componentsChanged();
    if (m_options.autoUpdate && !m_autoUpdateDone && updatesAvailable() > 0) {
        m_autoUpdateDone = true;   // once per run: a retry is a person's decision
        startUpdate();
    }
}

void UpdateController::finishUpdate(int exitCode, bool crashed)
{
    m_tick.stop();
    if (m_log.isOpen()) {
        logLine(QString("exit %1").arg(exitCode));
        m_log.close();
    }
    QFile::remove(m_options.lockFile);
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);

    m_powerCycleRequired = false;
    if (crashed) {
        setOutcome("error", "The update tool stopped unexpectedly",
                   "Check the firmware again before doing anything else. The log is in " + m_options.logDir + ".", false);
    } else if (exitCode == 0 && m_options.dryRun) {
        setOutcome("info", "Dry run complete",
                   "The images were checked against each board. Nothing was written.", false);
    } else if (exitCode == 0 && m_boardsUpdated > 0) {
        m_powerCycleRequired = true;
        writeNotice("Power cycle required");
        setOutcome("success", "Firmware updated",
                   "Switch the whole system off and on again to start the new firmware. "
                   "One power cycle is enough for all boards.", false);
    } else if (exitCode == 0) {
        setOutcome("info", "Nothing to update", "Every board already carries the firmware this system ships.", false);
    } else if (exitCode == 2) {
        setOutcome("warning", "The update did not finish",
                   "The system is still usable: every board is running firmware that works. You can try again now.", true);
    } else if (exitCode == 3) {
        setOutcome("error", "The update must be repeated now",
                   "A board is waiting in update mode. Try again now - a power cycle will not help.", true);
    } else if (exitCode == 4) {
        m_powerCycleRequired = true;
        writeNotice("Power cycle required");
        setOutcome("warning", "The new firmware was rolled back",
                   "A board returned to its previous firmware. It keeps working, but this release does not "
                   "run on it. Contact service; do not repeat the update.", false);
    } else if (exitCode == 5) {
        setOutcome("error", "Update refused",
                   "This unit already rolled this exact firmware back once. Installing it again would repeat "
                   "that. A fixed release is needed; nothing was changed.", false);
    } else if (exitCode == 6) {
        setOutcome("error", "A board stopped answering",
                   "Switch the system off and on again, then open System Manager to check the result.", false);
    } else {
        setOutcome("error", "The update could not start",
                   QString("Nothing was changed (exit %1). The log is in %2.").arg(exitCode).arg(m_options.logDir), true);
    }
    setState("done");
}

void UpdateController::setOutcome(const QString &kind, const QString &title,
                                  const QString &detail, bool canRetry)
{
    m_outcomeKind = kind;
    m_outcomeTitle = title;
    m_outcomeDetail = detail;
    m_canRetry = canRetry;
    emit outcomeChanged();
}

void UpdateController::writeNotice(const QString &text)
{
    if (m_options.noticeFile.isEmpty()) return;
    QFile f(m_options.noticeFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(text.toUtf8() + "\n");
    }
}

void UpdateController::logLine(const QString &line)
{
    qDebug().noquote() << line;
    if (m_log.isOpen()) {
        QTextStream(&m_log) << QDateTime::currentDateTime().toString("HH:mm:ss ") << line << "\n";
    }
}

QString UpdateController::formatVersion(const QString &raw)
{
    // Board registers read "0108" (BCD major, minor); files read "01.08"
    if (raw.isEmpty() || raw == "-") return QString::fromUtf8("—");
    if (raw.contains('.')) return "v" + raw;
    if (raw.length() == 4) return "v" + raw.left(2) + "." + raw.mid(2);
    return "v" + raw;
}

QVariantMap UpdateController::describe(const QMap<QString, QString> &f, bool updating)
{
    QVariantMap m;
    const QString board = f.value("board");
    const QString status = f.value("status");
    const QString image = f.value("image", "-");

    m["board"] = board;
    m["status"] = status;
    m["installed"] = formatVersion(f.value("version"));
    m["shipped"] = formatVersion(f.value("file_version"));
    m["image"] = image == "-" ? QString() : image;

    if (board == "983") {
        m["name"] = "983HH serializer board";
        m["role"] = "Sends video and touch to the display";
        m["icon"] = "board";
    } else {
        QString variant = "Display controller";
        if (image.startsWith("REMOTE_DISP_OTS")) variant = "Display controller (OLED OTS)";
        else if (image.startsWith("REMOTE_DISP_SPARTAN7")) variant = "Display controller (Spartan-7)";
        else if (image.startsWith("REMOTE_DISP")) variant = "Display controller (remote display)";
        m["name"] = variant;
        m["role"] = "Backlight, sensors and panel power in the display";
        m["icon"] = "display";
    }

    struct Label { const char *key; const char *text; const char *tone; const char *note; };
    static const Label labels[] = {
        {"uptodate",           "Up to date",        "ok",    ""},
        {"outdated",           "Update available",  "warn",  "This system ships different firmware for this board."},
        {"rolled-back-before", "Update refused",    "bad",   "This board already rolled this firmware back. A fixed release is needed."},
        {"no-bootloader",      "Service needed",    "bad",   "Older firmware without an update bootloader. It can only be updated with a wired flash."},
        {"no-image",           "No firmware here",  "muted", "This system ships no firmware for this board."},
        {"not-present",        "Not fitted",        "muted", "This unit has no controller here."},
        {"unknown",            "Could not read",    "muted", "The board did not answer the version check."},
        {"ok",                 "Updated",           "ok",    "Takes effect after the power cycle."},
        {"failed",             "Update failed",     "bad",   "See the result below."},
        {"skipped",            "Not changed",       "muted", ""},
    };
    m["statusText"] = status;
    m["tone"] = "muted";
    m["note"] = QString();
    for (const Label &l : labels) {
        if (status == l.key) {
            m["statusText"] = QString::fromUtf8(l.text);
            m["tone"] = QString::fromUtf8(l.tone);
            m["note"] = QString::fromUtf8(l.note);
        }
    }
    if (updating && status == "skipped") m["note"] = "Dry run: nothing was written.";
    if (board != "983" && status == "not-present") m["note"] = "This display has no controller of its own.";
    return m;
}
