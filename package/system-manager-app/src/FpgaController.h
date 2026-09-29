#ifndef FPGACONTROLLER_H
#define FPGACONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

/**
 * FpgaController - System Manager's "Display FPGA" section: A/B update of the
 * display FPGA over I2C (Spartan-7 boards today; sp6bins
 * docs/fpga-ab-update-procedure.md).
 *
 * Like the firmware section, the app never opens the I2C bus itself. Everything
 * goes through update-fpga.sh (space6-architecture, installed beside disptool):
 *
 *   probe()     update-fpga.sh --probe      two register reads: is there an FPGA
 *                                           with the update interface (0x1E)? The
 *                                           section exists only when there is.
 *   check()     update-fpga.sh --check      read-only slot scan (~6 s)
 *   update      update-fpga.sh              write <name>_ota.bin to the UPDATE slot
 *                                           (resumes; ~13 min when the whole slot
 *                                           differs); PROGRESS lines drive the bar
 *   activate    update-fpga.sh --activate   the display IOC cycles the FPGA's rails,
 *                                           the new image boots and is verified; the
 *                                           app then restarts the system
 *
 * Exit codes (the procedure's table): 0 ok, 1 blocked / failed (reason=),
 * 2 did not finish, nothing erased - rerun; 3 did not finish after the slot
 * header was erased - rerun now; 6 the FPGA stopped answering - power cycle,
 * then rerun (it resumes); 10 --check: an update is available.
 */
class FpgaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool present READ present NOTIFY presentChanged)
    Q_PROPERTY(QString display READ display NOTIFY presentChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY presentChanged)
    // probing | absent | checking | ready | updating | written | activating
    // | rebooting | failed
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // from the last check: current | outdated | blocked | unknown | no-answer
    Q_PROPERTY(QString status READ status NOTIFY resultChanged)
    Q_PROPERTY(QString reason READ reason NOTIFY resultChanged)
    Q_PROPERTY(QString image READ image NOTIFY resultChanged)
    Q_PROPERTY(QString runningRelease READ runningRelease NOTIFY resultChanged)
    Q_PROPERTY(QString runningBuild READ runningBuild NOTIFY resultChanged)
    Q_PROPERTY(QString runningSlot READ runningSlot NOTIFY resultChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY resultChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY progressChanged)
    Q_PROPERTY(QString phaseText READ phaseText NOTIFY progressChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY progressChanged)
    Q_PROPERTY(QString outcomeKind READ outcomeKind NOTIFY stateChanged)
    Q_PROPERTY(QString outcomeTitle READ outcomeTitle NOTIFY stateChanged)
    Q_PROPERTY(QString outcomeDetail READ outcomeDetail NOTIFY stateChanged)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY stateChanged)
    Q_PROPERTY(bool powerCycleRequired READ powerCycleRequired NOTIFY stateChanged)
    Q_PROPERTY(bool dryRun READ dryRun CONSTANT)

public:
    struct Options {
        QString tool;           // update-fpga.sh
        QString imageDir;       // the <name>_ota.bin images
        QString logDir;         // this app's per-run logs, and update-fpga.sh's (LOG_DIR)
        QString lockFile;       // shared with the other sections and the badge
        QString noticeFile;     // launcher header notice
        QString rebootCommand = "systemctl reboot";
        bool dryRun = false;
        bool autoUpdate = false;     // automated validation only: update once a check offers one
        bool autoActivate = false;   // automated validation only: activate once the image is written
    };

    explicit FpgaController(const Options &options, QObject *parent = nullptr);
    ~FpgaController() override;

    bool present() const { return m_present; }
    QString display() const { return m_display; }
    QString displayName() const;
    QString state() const { return m_state; }
    QString status() const { return m_status; }
    QString reason() const { return m_reason; }
    QString image() const { return m_image; }
    QString runningRelease() const;
    QString runningBuild() const { return m_runBuild; }
    QString runningSlot() const { return m_runSlot; }
    bool updateAvailable() const { return m_status == "outdated"; }
    QString phase() const { return m_phase; }
    QString phaseText() const;
    int percent() const { return m_percent; }
    int elapsedSeconds() const;
    QString outcomeKind() const { return m_outcomeKind; }
    QString outcomeTitle() const { return m_outcomeTitle; }
    QString outcomeDetail() const { return m_outcomeDetail; }
    bool canRetry() const { return m_canRetry; }
    bool powerCycleRequired() const { return m_powerCycleRequired; }
    bool dryRun() const { return m_options.dryRun; }

    Q_INVOKABLE void probe();
    Q_INVOKABLE void check();
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE void activate();
    Q_INVOKABLE bool canQuit() const;

signals:
    void presentChanged();
    void stateChanged();
    void resultChanged();
    void progressChanged();

private:
    enum class Mode { None, Probe, Check, Update, Activate };

    void run(Mode mode, const QStringList &args);
    void onOutput();
    void handleLine(const QString &line);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void finishUpdate(int exitCode);
    void finishActivate(int exitCode);
    void setState(const QString &state);
    void setOutcome(const QString &kind, const QString &title, const QString &detail,
                    bool canRetry, bool powerCycle = false);
    void holdForUpdate(bool on);
    void writeNotice(const QString &text);
    void logLine(const QString &line);
    bool realTool() const;

    Options m_options;
    QProcess *m_process = nullptr;
    Mode m_mode = Mode::None;
    QByteArray m_pending;

    bool m_present = false;
    QString m_display;
    QString m_state = "probing";
    QString m_status;
    QString m_reason;
    QString m_image;
    QString m_runRelease;   // 0x1D reg 0x00: MM DD 00 VV
    QString m_runBuild;     // 0x1D reg 0x14: YY HH MM SS
    QString m_runSlot;      // OTA | GOLDEN
    QString m_phase;
    int m_percent = 0;
    QString m_outcomeKind;
    QString m_outcomeTitle;
    QString m_outcomeDetail;
    bool m_canRetry = false;
    bool m_powerCycleRequired = false;
    QElapsedTimer m_clock;
    QTimer m_tick;
    QFile m_log;
    bool m_autoUpdateDone = false;
    bool m_autoActivateDone = false;
};

#endif // FPGACONTROLLER_H
