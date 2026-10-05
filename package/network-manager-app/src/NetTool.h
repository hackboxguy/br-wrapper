#ifndef NETTOOL_H
#define NETTOOL_H

#include <QObject>
#include <QProcess>
#include <QVariantMap>
#include <QStringList>

/**
 * NetTool - runs net-ctl.sh for one controller, one command at a time.
 *
 * net-ctl.sh is the app's only link to NetworkManager (plan, section 3). It
 * prints the handover's line protocol:
 *
 *   RESULT key=value ... [reason=<free text, last>]
 *   PROGRESS phase=<name> ...
 *   NOTICE <text>
 *
 * Values are percent-encoded by the script (an SSID is arbitrary bytes);
 * parseLine() decodes them. "reason" is the one key whose value runs to the
 * end of the line and is taken as it stands.
 *
 * Reads run as the app user. Changes run as "sudo -n net-ctl.sh ..." (the
 * launcher starts apps as pi, which has passwordless sudo on these images; -n
 * fails at once instead of hanging on a prompt nobody can see). In a dry run
 * nothing is elevated and changes get --dry-run.
 *
 * A secret (a WiFi password) goes to the script on stdin and nowhere else:
 * not in the argument list, not in the log.
 */
class NetTool : public QObject
{
    Q_OBJECT
public:
    struct Line {
        enum Kind { Result, Progress, Notice, Other };
        Kind kind = Other;
        QVariantMap fields;   // Result, Progress
        QString text;         // Notice, Other: the text after the tag
    };

    // The parser, on its own for tests/test_parser.cpp
    static Line parseLine(const QString &raw);
    static QString percentDecode(const QString &value);
    static QString percentEncode(const QString &value);

    // Every controller's lines go to one log (volatile: /tmp)
    static void log(const QString &line);
    static void setLogFile(const QString &path);

    explicit NetTool(const QString &tool, bool dryRun, QObject *parent = nullptr);
    ~NetTool() override;

    bool busy() const;
    QString command() const { return m_command; }
    // the running change said "NOTICE detached": it runs as its own unit
    bool detached() const { return m_detached; }

    // elevated: run through sudo -n (never in a dry run). stdinData is written
    // and the channel closed; without it stdin is closed at once.
    void run(const QStringList &args, bool elevated = false, const QByteArray &stdinData = QByteArray());
    void stop();

signals:
    void result(const QVariantMap &fields);
    void progress(const QVariantMap &fields);
    void notice(const QString &text);
    void finished(int exitCode);

private:
    void onOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void handle(const QString &raw);

    QString m_tool;
    bool m_dryRun = false;
    QProcess *m_process = nullptr;
    QByteArray m_pending;
    QString m_command;
    bool m_stopping = false;
    bool m_detached = false;    // the change said it runs as its own unit
    QStringList m_quietLines;   // a read's output, logged only if it fails
};

#endif // NETTOOL_H
