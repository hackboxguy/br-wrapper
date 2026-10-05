#include "NetTool.h"

#include <QDateTime>
#include <QFile>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>
#include <unistd.h>

static QString &logPath()
{
    static QString path = "/tmp/network-manager-app.log";
    return path;
}

void NetTool::setLogFile(const QString &path)
{
    logPath() = path;
}

void NetTool::log(const QString &line)
{
    qDebug().noquote() << line;
    if (logPath().isEmpty()) return;
    QFile f(logPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        QTextStream(&f) << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss ") << line << "\n";
}

QString NetTool::percentDecode(const QString &value)
{
    if (!value.contains('%')) return value;
    return QString::fromUtf8(QByteArray::fromPercentEncoding(value.toUtf8()));
}

QString NetTool::percentEncode(const QString &value)
{
    // The script's set: everything outside [A-Za-z0-9._~:/,@+-] is %XX
    return QString::fromLatin1(value.toUtf8().toPercentEncoding(":/,@+"));
}

NetTool::Line NetTool::parseLine(const QString &raw)
{
    static const QRegularExpression ansi("\\x1B\\[[0-9;]*[A-Za-z]");
    QString text = raw;
    text.remove(ansi);
    text = text.trimmed();

    Line line;
    QString body;
    if (text.startsWith("RESULT ") || text == "RESULT") {
        line.kind = Line::Result;
        body = text.mid(7);
    } else if (text.startsWith("PROGRESS ") || text == "PROGRESS") {
        line.kind = Line::Progress;
        body = text.mid(9);
    } else if (text.startsWith("NOTICE ")) {
        line.kind = Line::Notice;
        line.text = text.mid(7).trimmed();
        return line;
    } else {
        line.text = text;
        return line;
    }

    // key=value pairs split at spaces; values carry no raw spaces (the script
    // encodes them), except "reason", which is free text to the end
    int pos = 0;
    const int n = body.size();
    while (pos < n) {
        while (pos < n && body.at(pos) == ' ') ++pos;
        if (pos >= n) break;
        int end = body.indexOf(' ', pos);
        if (end < 0) end = n;
        const QString token = body.mid(pos, end - pos);
        const int eq = token.indexOf('=');
        if (eq > 0) {
            const QString key = token.left(eq);
            if (key == "reason") {
                line.fields.insert(key, body.mid(pos + eq + 1).trimmed());
                break;
            }
            line.fields.insert(key, percentDecode(token.mid(eq + 1)));
        }
        pos = end + 1;
    }
    return line;
}

// The periodic reads would fill /tmp with the same lines every few seconds:
// their output is logged only when they fail
static bool quietCommand(const QString &command)
{
    return command == "status" || command == "wifi-scan" || command == "leases" || command == "available";
}

// The tools stream a line per reply or per second: the log keeps the command,
// the summaries and the exit, not every sample
static bool toolCommand(const QString &command)
{
    return command == "ping" || command == "internet-check" || command == "iperf-server" || command == "iperf-client";
}

NetTool::NetTool(const QString &tool, bool dryRun, QObject *parent)
    : QObject(parent), m_tool(tool), m_dryRun(dryRun)
{
}

NetTool::~NetTool()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        if (m_detached) {
            // A change that runs as its own systemd unit (rule 6a): the client
            // (sudo, systemd-run) goes; the unit finishes or restores alone.
            // SIGKILL, not SIGTERM: sudo would relay a SIGTERM to systemd-run
            log(m_command + " runs on as its own unit");
            m_process->kill();
            m_process->waitForFinished(1000);
        } else if (quietCommand(m_command) || m_command == "monitor" || toolCommand(m_command)) {
            m_process->terminate();
            if (!m_process->waitForFinished(1000)) m_process->kill();
            m_process->waitForFinished(1000);
        } else {
            // A change finishes or restores on its own (the script ignores
            // SIGTERM meanwhile); killing it would leave half a change
            log("waiting for " + m_command + " to finish");
            m_process->waitForFinished(-1);
        }
    }
}

bool NetTool::busy() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void NetTool::run(const QStringList &args, bool elevated, const QByteArray &stdinData)
{
    if (busy()) {
        log("net-ctl busy, dropped: " + args.join(' '));
        return;
    }
    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, &NetTool::onOutput);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &NetTool::onFinished);
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) onFinished(127, QProcess::CrashExit);
        });
    }
    m_pending.clear();
    m_stopping = false;
    m_detached = false;
    m_command = args.value(0);

    QString program = m_tool;
    QStringList fullArgs = args;
    if (elevated && !m_dryRun && ::geteuid() != 0) {
        fullArgs.prepend(program);
        // sudo resets the environment: the script's NET_CTL_* settings (test
        // seams such as NET_CTL_INCLUDE_VETH) go along as VAR=value
        for (const QString &kv : QProcessEnvironment::systemEnvironment().toStringList()) {
            if (kv.startsWith("NET_CTL_")) fullArgs.prepend(kv);
        }
        fullArgs.prepend("-n");
        program = "sudo";
    }
    // The argument list never holds a secret; stdin does, and is not logged
    const QString commandLine = QString("$ %1 %2%3").arg(program, fullArgs.join(' '),
                                                         stdinData.isEmpty() ? QString() : QString("  (secret on stdin)"));
    m_quietLines.clear();
    if (quietCommand(m_command)) m_quietLines << commandLine;
    else log(commandLine);
    m_process->start(program, fullArgs);
    if (!stdinData.isEmpty()) m_process->write(stdinData);
    m_process->closeWriteChannel();
}

void NetTool::stop()
{
    if (!busy()) return;
    m_stopping = true;
    m_process->terminate();
}

void NetTool::onOutput()
{
    m_pending += m_process->readAll();
    int nl;
    while ((nl = m_pending.indexOf('\n')) >= 0) {
        const QString raw = QString::fromUtf8(m_pending.left(nl));
        m_pending.remove(0, nl + 1);
        handle(raw);
    }
}

void NetTool::handle(const QString &raw)
{
    const Line line = parseLine(raw);
    if (quietCommand(m_command)) {
        if (!raw.trimmed().isEmpty()) m_quietLines << raw.trimmed();
        switch (line.kind) {
        case Line::Result: emit result(line.fields); break;
        case Line::Progress: emit progress(line.fields); break;
        case Line::Notice: emit notice(line.text); break;
        case Line::Other: break;
        }
        return;
    }
    switch (line.kind) {
    case Line::Result: {
        const QString kind = line.fields.value("kind").toString();
        if (!(toolCommand(m_command) && (kind == "reply" || kind == "lost" || kind == "iperf"))) log(raw.trimmed());
        emit result(line.fields);
        break;
    }
    case Line::Other:
        if (toolCommand(m_command)) break;   // the tool's own output, already parsed
        if (!line.text.isEmpty()) log("  " + line.text);
        break;
    case Line::Progress:
        log(raw.trimmed());
        emit progress(line.fields);
        break;
    case Line::Notice:
        if (line.text == "detached") m_detached = true;
        if (line.text != "changed") log(raw.trimmed());
        emit notice(line.text);
        break;
    }
}

void NetTool::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_process) onOutput();
    if (!m_pending.isEmpty()) {
        handle(QString::fromUtf8(m_pending));
        m_pending.clear();
    }
    const int code = (status == QProcess::NormalExit) ? exitCode : (m_stopping ? 130 : 128);
    if (quietCommand(m_command)) {
        if (code != 0 && !m_stopping && !(m_command == "available" && code == 4)) {
            for (const QString &l : m_quietLines) log(l);
            log(QString("exit %1 (%2)").arg(code).arg(m_command));
        } else if (m_command == "available") {
            log(m_quietLines.value(1, m_quietLines.value(0)));   // once per start: the answer
        }
        m_quietLines.clear();
    } else if (m_command != "monitor" || code != 130) {
        log(QString("exit %1 (%2)").arg(code).arg(m_command));
    }
    m_stopping = false;
    emit finished(code);
}
