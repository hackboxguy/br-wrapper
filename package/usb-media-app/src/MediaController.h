#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QVector>

// One file on the stick, as the list shows it
struct MediaFile {
    QString rel;          // path relative to the stick root, "/" separated
    QString name;
    QString folder;       // "" for the root
    QString kind;         // "video", "image"
    QString decode;       // "hw", "sw", "image", "unsupported", "" (not probed yet)
    QString info;         // "1920x1080 · 30 fps · 0:20 · H.264"
    QString note;         // "may play slowly", "plays unrotated"
    QString reason;       // why it cannot play
    bool checked = false;
    bool probed = false;
    bool playable() const { return probed && decode != "unsupported"; }
};

class MediaModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole + 1, FolderRole, KindRole, DecodeRole, InfoRole, NoteRole,
                 ReasonRole, CheckedRole, ProbedRole, PlayableRole, OrderRole };
    explicit MediaModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVector<MediaFile> &files() { return m_files; }
    const QVector<MediaFile> &constFiles() const { return m_files; }
    void reset(const QVector<MediaFile> &files);
    void changed(int row);
    void changedAll();
    bool move(int row, int delta);

private:
    QVector<MediaFile> m_files;
};

class MediaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *files READ files CONSTANT)
    Q_PROPERTY(QString root READ root CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(int probedCount READ probedCount NOTIFY progressChanged)
    Q_PROPERTY(int mediaCount READ mediaCount NOTIFY progressChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY selectionChanged)
    Q_PROPERTY(int imageDuration READ imageDuration WRITE setImageDuration NOTIFY settingsChanged)
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY settingsChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY settingsChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(QString messageTone READ messageTone NOTIFY messageChanged)

public:
    struct Options {
        QString root;
        QString player;
        QString tempPlaylist;
        QString message;
        QString probeCache;    // classifications kept across the app <-> player loop
    };
    explicit MediaController(const Options &options, QObject *parent = nullptr);

    QObject *files() { return &m_model; }
    QString root() const { return m_opt.root; }
    bool readOnly() const { return m_readOnly; }
    QString state() const { return m_state; }
    int probedCount() const { return m_probedCount; }
    int mediaCount() const { return m_mediaCount; }
    int checkedCount() const;
    int imageDuration() const { return m_imageDuration; }
    void setImageDuration(int s);
    bool loop() const { return m_loop; }
    void setLoop(bool on);
    bool autostart() const { return m_autostart; }
    void setAutostart(bool on);
    bool dirty() const { return m_dirty; }
    QString message() const { return m_message; }
    void setMessage(const QString &m) { setMessageTone(m, "warn"); }
    QString messageTone() const { return m_messageTone; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void selectAll(bool on);
    Q_INVOKABLE bool move(int row, int delta);
    Q_INVOKABLE bool save();
    Q_INVOKABLE void play();
    Q_INVOKABLE void back();
    Q_INVOKABLE void playSaved();   // the autostart countdown ran out: play the stick's playlist

    static const int kPlayExitCode = 10;
    static const char *kPlaylistName;

signals:
    void stateChanged();
    void progressChanged();
    void selectionChanged();
    void settingsChanged();
    void dirtyChanged();
    void messageChanged();

private:
    void scan();
    void loadPlaylist(QVector<MediaFile> &files);
    void probeNext();
    void onProbeFinished();
    bool applyProbeLine(const QString &line);
    void loadProbeCache();
    void saveProbeCache();
    QString fileStamp(const QString &path) const;
    QByteArray playlistJson() const;
    bool writeSafely(const QString &path, const QByteArray &data, QString *error) const;
    void setDirty(bool d);
    void setState(const QString &s);
    void setMessageTone(const QString &m, const QString &tone);

    Options m_opt;
    MediaModel m_model;
    bool m_readOnly = false;
    QString m_state = "scanning";
    int m_probedCount = 0;
    int m_mediaCount = 0;
    int m_imageDuration = 10;
    bool m_loop = true;
    bool m_autostart = false;
    bool m_dirty = false;
    QString m_message;
    QString m_messageTone = "warn";
    QJsonObject m_loaded;      // the playlist as read: unknown keys survive a save
    QProcess m_probe;
    QStringList m_batch;       // absolute paths in the running probe
    int m_nextProbe = 0;
    QHash<QString, QString> m_cache;   // absolute path -> "<size>:<mtime>\t<PROBE line>"
    bool m_cacheDirty = false;
};

#endif
