#ifndef UPDATECONTROLLER_H
#define UPDATECONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QFile>
#include <QVariantList>
#include <QTimer>

/**
 * UpdateController - the System Manager firmware section's link to update-iocs.sh.
 *
 * This app never opens the I2C bus itself. Everything goes through
 * update-iocs.sh (space6-architecture, installed beside disptool):
 *
 *   check()        update-iocs.sh --check --image-dir <dir>      (read-only)
 *   startUpdate()  update-iocs.sh --image-dir <dir> --keep-stream [--dry-run]
 *
 * The script picks each board's image pair from the shipped directory by the
 * board code the board runs, quiesces the drivers and services that share the
 * bus, updates the display IOC first and the 983HH second, and ends with one
 * RESULT line per board plus an exit code. That keeps the rule "no second I2C
 * master during an update" in the one place that already enforces it, and
 * gives this UI a single, tested source of truth.
 *
 * Exit codes of an update (docs/field-update-procedure.md in
 * rh850-baremetal-demo):
 *   0 updated (power cycle required), 1 preconditions, 2 failed but usable --
 *   retry, 3 no valid slot -- retry now, 4 rolled back, 5 refused (never retry
 *   the same image), 6 a board stopped answering.
 */
class UpdateController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QVariantList components READ components NOTIFY componentsChanged)
    Q_PROPERTY(int updatesAvailable READ updatesAvailable NOTIFY componentsChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY componentsChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY activityChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY activityChanged)
    Q_PROPERTY(QString outcomeKind READ outcomeKind NOTIFY outcomeChanged)
    Q_PROPERTY(QString outcomeTitle READ outcomeTitle NOTIFY outcomeChanged)
    Q_PROPERTY(QString outcomeDetail READ outcomeDetail NOTIFY outcomeChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY outcomeChanged)
    Q_PROPERTY(bool powerCycleRequired READ powerCycleRequired NOTIFY outcomeChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)
    Q_PROPERTY(QString checkError READ checkError NOTIFY componentsChanged)

public:
    struct Options {
        QString tool;        // update-iocs.sh
        QString imageDir;    // shipped *_ota.bin / *_otaB.bin pairs
        QString logDir;      // one log file per update run
        QString noticeFile;  // launcher header notice ("Power cycle required")
        QString lockFile;    // present while an update runs (badge check skips)
        bool dryRun = false;
        bool autoUpdate = false;   // automated validation: start the update after a check that finds one
    };

    explicit UpdateController(const Options &options, QObject *parent = nullptr);
    ~UpdateController() override;

    QString state() const { return m_state; }
    QVariantList components() const { return m_components; }
    int updatesAvailable() const;
    QString summary() const;
    QString activity() const { return m_activity; }
    int elapsedSeconds() const;
    QString outcomeKind() const { return m_outcomeKind; }
    QString outcomeTitle() const { return m_outcomeTitle; }
    QString outcomeDetail() const { return m_outcomeDetail; }
    bool canRetry() const { return m_canRetry; }
    bool powerCycleRequired() const { return m_powerCycleRequired; }
    bool dryRun() const { return m_options.dryRun; }
    QString checkError() const { return m_checkError; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE bool canQuit() const { return m_state != "updating"; }
    Q_INVOKABLE void quitApp();

signals:
    void stateChanged();
    void componentsChanged();
    void activityChanged();
    void outcomeChanged();

private:
    enum class Mode { None, Check, Update };

    void setState(const QString &state);
    void runTool(Mode mode, const QStringList &args);
    void onOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void handleLine(const QString &line);
    void applyResult(const QMap<QString, QString> &fields);
    void finishCheck(int exitCode);
    void finishUpdate(int exitCode, bool crashed);
    void setOutcome(const QString &kind, const QString &title, const QString &detail, bool canRetry);
    void writeNotice(const QString &text);
    void logLine(const QString &line);

    static QVariantMap describe(const QMap<QString, QString> &fields, bool updating);
    static QString formatVersion(const QString &raw);

    Options m_options;
    QProcess *m_process = nullptr;
    Mode m_mode = Mode::None;
    QString m_state = "checking";
    QVariantList m_components;
    QString m_activity;
    QString m_checkError;
    QString m_outcomeKind;
    QString m_outcomeTitle;
    QString m_outcomeDetail;
    bool m_canRetry = false;
    bool m_powerCycleRequired = false;
    QByteArray m_pending;
    QElapsedTimer m_clock;
    QTimer m_tick;
    QFile m_log;
    int m_resultsSeen = 0;
    int m_boardsUpdated = 0;
    bool m_autoUpdateDone = false;
};

#endif // UPDATECONTROLLER_H
