#ifndef SYSTEMIMAGECONTROLLER_H
#define SYSTEMIMAGECONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <QVariantMap>

/**
 * SystemImageController - System Manager's "System image" section: installs a
 * new SD-card image from a USB stick into the inactive A/B slot.
 *
 * A front end to pi-ab-update, never a second copy of its policy. It composes:
 *
 *   system-image-scan.sh               what is on the stick (read-only; mirrors
 *                                      the engine's USB rule so the user is told
 *                                      before pressing the button)
 *   ab-update install usb              the install; the engine streams, verifies,
 *                                      arms and REBOOTS by itself
 *   <runtime-dir>/progress             phase= / progress= while it runs
 *   <runtime-dir>/status               state=committed|candidate-armed|fallback
 *                                      after a reboot (the commit service)
 *
 * It never touches partitions, config.txt, tryboot or the reboot, and never
 * decides what is installable - apart from not offering what the engine is
 * certain to refuse (no bundle, several, a bad signature, the same version),
 * and one guard the engine leaves to the UI: no install while the running
 * image is still an uncommitted candidate.
 */
class SystemImageController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(QString runningVersion READ runningVersion CONSTANT)
    Q_PROPERTY(QString slot READ slot CONSTANT)
    Q_PROPERTY(QString variant READ variant CONSTANT)
    // committed | candidate-armed | fallback, or empty when no update has run
    Q_PROPERTY(QString lastOutcome READ lastOutcome NOTIFY outcomeChanged)
    // One line for the top of the section after a reboot, empty when nothing to say
    Q_PROPERTY(QString lastOutcomeText READ lastOutcomeText NOTIFY outcomeChanged)
    // idle | scanning | nostick | none | nested | many | one (not installable,
    // see scanDetail) | same-version | ready | unreadable (a filesystem would
    // not mount and no bundle was found) | error
    Q_PROPERTY(QString scanState READ scanState NOTIFY scanChanged)
    Q_PROPERTY(QVariantMap offered READ offered NOTIFY scanChanged)
    Q_PROPERTY(QString scanDetail READ scanDetail NOTIFY scanChanged)
    // idle | installing | arming | failed
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY progressChanged)
    Q_PROPERTY(QString phaseText READ phaseText NOTIFY progressChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY progressChanged)
    Q_PROPERTY(QString failureClass READ failureClass NOTIFY stateChanged)
    Q_PROPERTY(QString outcomeTitle READ outcomeTitle NOTIFY stateChanged)
    Q_PROPERTY(QString outcomeDetail READ outcomeDetail NOTIFY stateChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY stateChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY scanChanged)
    // UI-side preflight: the engine commits a candidate only if every
    // AB_HEALTH_UNITS unit stays active with no restarts. When one already is
    // not, say so on the offer - and still allow the install, as the engine
    // does (someone recovering a device needs exactly that). Empty when fine.
    Q_PROPERTY(QString preflightWarning READ preflightWarning NOTIFY preflightChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)
    // The section sets this while it is on screen: USB polling runs only then
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    struct Options {
        QString abUpdate;        // /usr/local/bin/ab-update
        QString scanTool;        // system-image-scan.sh
        QString runtimeDir;      // the engine's AB_RUNTIME_DIR
        QString imageManifest;   // the running image's image-manifest.env
        QString logDir;          // one log per install run
        QString stateFile;       // what this app last started to install (for the outcome line)
        QString lockFile;        // shared with the firmware section and the badge
        QString abConfig;        // the engine's board config (AB_HEALTH_UNITS)
        QString systemctl = "systemctl";   // seam for tests
        QString ackFile;         // acknowledged-fallback: the badge stops repeating a seen fallback
        bool dryRun = false;
        bool autoInstall = false;   // automated validation only: install once a scan offers one
    };

    explicit SystemImageController(const Options &options, QObject *parent = nullptr);
    ~SystemImageController() override;

    bool supported() const { return m_supported; }
    QString runningVersion() const { return m_runningVersion; }
    QString slot() const { return m_slot; }
    QString variant() const { return m_variant; }
    QString lastOutcome() const { return m_lastOutcome; }
    QString lastOutcomeText() const;
    QString scanState() const { return m_scanState; }
    QVariantMap offered() const { return m_offered; }
    QString scanDetail() const { return m_scanDetail; }
    QString state() const { return m_state; }
    QString phase() const { return m_phase; }
    QString phaseText() const;
    int percent() const { return m_percent; }
    int elapsedSeconds() const;
    QString failureClass() const { return m_failureClass; }
    QString outcomeTitle() const { return m_outcomeTitle; }
    QString outcomeDetail() const { return m_outcomeDetail; }
    bool canRetry() const { return m_canRetry; }
    bool canInstall() const;
    QString preflightWarning() const { return m_preflightWarning; }
    bool dryRun() const { return m_options.dryRun; }
    bool active() const { return m_active; }
    void setActive(bool active);

    // True when the launcher should open this section first: an outcome to
    // report after a reboot, or an installable bundle already on a stick
    Q_INVOKABLE bool wantsAttention() const;
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void startInstall();
    Q_INVOKABLE bool canQuit() const { return m_state != "installing" && m_state != "arming"; }
    Q_INVOKABLE void acknowledgeFailure();

signals:
    void outcomeChanged();
    void scanChanged();
    void stateChanged();
    void progressChanged();
    void activeChanged();
    void preflightChanged();

private:
    void readRunningImage();
    void readStatus();
    void runPreflight();
    void acknowledgeFallback();
    QString runQuick(const QString &program, const QStringList &args, int timeoutMs) const;
    QStringList sudoWrap(QString &program, QStringList args) const;
    void pollUsb();
    QString usbFingerprint() const;
    void onScanFinished(int exitCode);
    void parseScan(const QString &out);
    void pollProgress();
    void onInstallOutput();
    void onInstallFinished(int exitCode, QProcess::ExitStatus status);
    void fail(const QString &cls);
    void setState(const QString &state);
    void logLine(const QString &line);
    void releaseLock();

    Options m_options;
    bool m_supported = false;
    QString m_runningVersion;
    QString m_slot;
    QString m_variant;
    QString m_lastOutcome;
    QString m_lastOfferedVersion;   // from stateFile: what the previous run installed
    QString m_lastFromVersion;

    QString m_scanState = "idle";
    QVariantMap m_offered;
    QString m_scanDetail;
    QProcess *m_scan = nullptr;
    bool m_rescanPending = false;
    QString m_lastFingerprint;

    QString m_state = "idle";
    QString m_phase;
    int m_percent = 0;
    QString m_failureClass;
    QString m_outcomeTitle;
    QString m_outcomeDetail;
    bool m_canRetry = false;
    int m_internalRetries = 0;
    bool m_autoInstallDone = false;

    QProcess *m_install = nullptr;
    QByteArray m_pending;
    QDateTime m_startedAt;
    QElapsedTimer m_clock;
    QFile m_log;

    bool m_active = false;
    QTimer m_usbTimer;       // 2 s, while the section is visible and idle
    QTimer m_progressTimer;  // 500 ms, while installing
    QTimer m_statusTimer;    // 5 s, while the running image is a candidate
    QTimer m_preflightTimer; // 5 s, while the section is visible
    QString m_preflightWarning;
};

#endif // SYSTEMIMAGECONTROLLER_H
