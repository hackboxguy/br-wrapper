#include "MediaController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

const char *MediaController::kPlaylistName = "micropanel-playlist.json";

namespace {

const int kMaxDepth = 3;           // stick root + 3 levels of folders
const int kProbeBatch = 16;        // files per dual-video-player --probe run
const int kMinImageDuration = 2;
const int kMaxImageDuration = 600;

const QStringList kVideoExt = { "mp4", "mkv", "mov", "m4v" };
const QStringList kImageExt = { "jpg", "jpeg", "png" };
// Listed (so nobody wonders where they went) but not playable
const QStringList kOtherVideoExt = { "avi", "wmv", "webm", "flv", "mpg", "mpeg", "ts", "3gp" };
const QStringList kOtherImageExt = { "heic", "heif", "gif", "bmp", "tif", "tiff", "webp" };

bool skipName(const QString &name)
{
    return name.startsWith('.') || name.startsWith('$') || name == "LOST.DIR"
        || name == "System Volume Information";
}

void scanDir(const QString &root, const QString &rel, int depth, QVector<MediaFile> &out)
{
    QDir dir(rel.isEmpty() ? root : root + "/" + rel);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                    QDir::Name | QDir::IgnoreCase | QDir::DirsLast);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (skipName(name))
            continue;
        const QString path = rel.isEmpty() ? name : rel + "/" + name;
        if (fi.isDir()) {
            if (depth < kMaxDepth && !fi.isSymLink())
                scanDir(root, path, depth + 1, out);
            continue;
        }
        const QString ext = fi.suffix().toLower();
        MediaFile f;
        f.rel = path;
        f.name = name;
        f.folder = rel;
        if (kVideoExt.contains(ext) || kOtherVideoExt.contains(ext))
            f.kind = "video";
        else if (kImageExt.contains(ext) || kOtherImageExt.contains(ext))
            f.kind = "image";
        else
            continue;
        if (kOtherVideoExt.contains(ext) || kOtherImageExt.contains(ext)) {
            f.probed = true;
            f.decode = "unsupported";
            f.reason = ext.toUpper() + (ext == "heic" || ext == "heif"
                                        ? " not supported (save as JPEG)" : " not supported");
        }
        out.append(f);
    }
}

QString duration(double s)
{
    const int t = qRound(s);
    return t >= 3600 ? QString("%1:%2:%3").arg(t / 3600).arg(t / 60 % 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
}

QString codecName(const QString &c)
{
    if (c == "h264") return "H.264";
    if (c == "h265") return "HEVC";
    if (c == "jpeg") return "JPEG";
    if (c == "png") return "PNG";
    return c == "-" ? QString() : c.toUpper();
}

} // namespace

// ---- model ------------------------------------------------------------------

int MediaModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_files.size();
}

QVariant MediaModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_files.size())
        return QVariant();
    const MediaFile &f = m_files[index.row()];
    switch (role) {
    case NameRole: return f.name;
    case FolderRole: return f.folder;
    case KindRole: return f.kind;
    case DecodeRole: return f.decode;
    case InfoRole: return f.info;
    case NoteRole: return f.note;
    case ReasonRole: return f.reason;
    case CheckedRole: return f.checked;
    case ProbedRole: return f.probed;
    case PlayableRole: return f.playable();
    case OrderRole: {
        // position in the playlist (1-based) for checked rows, else 0
        if (!f.checked)
            return 0;
        int n = 0;
        for (int i = 0; i <= index.row(); ++i)
            if (m_files[i].checked)
                ++n;
        return n;
    }
    }
    return QVariant();
}

QHash<int, QByteArray> MediaModel::roleNames() const
{
    return { { NameRole, "name" }, { FolderRole, "folder" }, { KindRole, "kind" }, { DecodeRole, "decode" },
             { InfoRole, "info" }, { NoteRole, "note" }, { ReasonRole, "reason" }, { CheckedRole, "checked" },
             { ProbedRole, "probed" }, { PlayableRole, "playable" }, { OrderRole, "order" } };
}

void MediaModel::reset(const QVector<MediaFile> &files)
{
    beginResetModel();
    m_files = files;
    endResetModel();
}

void MediaModel::changed(int row)
{
    emit dataChanged(index(row), index(row));
}

void MediaModel::changedAll()
{
    if (!m_files.isEmpty())
        emit dataChanged(index(0), index(m_files.size() - 1));
}

bool MediaModel::move(int row, int delta)
{
    const int to = row + delta;
    if (row < 0 || row >= m_files.size() || to < 0 || to >= m_files.size() || delta == 0)
        return false;
    // beginMoveRows wants the destination as "insert before" in the old order
    beginMoveRows(QModelIndex(), row, row, QModelIndex(), delta > 0 ? to + 1 : to);
    m_files.move(row, to);
    endMoveRows();
    return true;
}

// ---- controller -------------------------------------------------------------

MediaController::MediaController(const Options &options, QObject *parent)
    : QObject(parent), m_opt(options)
{
    m_message = options.message;
    m_readOnly = ::access(QFile::encodeName(m_opt.root).constData(), W_OK) != 0;
    connect(&m_probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { onProbeFinished(); });
    connect(&m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            qWarning() << "usb-media-app: cannot run" << m_opt.player;
            onProbeFinished();
        }
    });
}

void MediaController::start()
{
    scan();
}

void MediaController::scan()
{
    setState("scanning");
    QVector<MediaFile> files;
    if (QFileInfo(m_opt.root).isDir())
        scanDir(m_opt.root, QString(), 1, files);
    else
        setMessageTone("The USB stick is not there any more", "bad");
    loadPlaylist(files);
    emit settingsChanged();
    m_model.reset(files);
    m_mediaCount = 0;
    m_probedCount = 0;
    for (const MediaFile &f : files) {
        ++m_mediaCount;
        if (f.probed)
            ++m_probedCount;
    }
    emit progressChanged();
    emit selectionChanged();
    m_nextProbe = 0;
    setState("probing");
    probeNext();
}

// Saved playlist: its items first, in its order and checked; then the rest
void MediaController::loadPlaylist(QVector<MediaFile> &files)
{
    QFile f(m_opt.root + "/" + kPlaylistName);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        setMessageTone(QString("%1 could not be read (%2); saving replaces it")
                       .arg(kPlaylistName, err.errorString()), "warn");
        return;
    }
    m_loaded = doc.object();
    m_imageDuration = qBound(kMinImageDuration, m_loaded.value("image_duration_s").toInt(10), kMaxImageDuration);
    m_loop = m_loaded.value("loop").toBool(false);
    m_autostart = m_loaded.value("autostart").toBool(false);

    QHash<QString, int> byRel;
    for (int i = 0; i < files.size(); ++i)
        byRel.insert(files[i].rel, i);
    QVector<MediaFile> ordered;
    QSet<int> used;
    int missing = 0;
    for (const QJsonValue &v : m_loaded.value("items").toArray()) {
        const int i = byRel.value(v.toString(), -1);
        if (i < 0) {
            ++missing;
            continue;
        }
        if (used.contains(i))
            continue;   // listed twice: the list shows each file once
        used.insert(i);
        files[i].checked = true;
        ordered.append(files[i]);
    }
    for (int i = 0; i < files.size(); ++i)
        if (!used.contains(i))
            ordered.append(files[i]);
    files = ordered;
    if (missing && m_message.isEmpty())
        setMessageTone(QString("%1 item%2 of the saved playlist %3 not on the stick")
                       .arg(missing).arg(missing == 1 ? "" : "s").arg(missing == 1 ? "is" : "are"), "warn");
}

void MediaController::probeNext()
{
    QVector<MediaFile> &files = m_model.files();
    m_batch.clear();
    while (m_nextProbe < files.size() && m_batch.size() < kProbeBatch) {
        const MediaFile &f = files[m_nextProbe++];
        if (!f.probed)
            m_batch << m_opt.root + "/" + f.rel;
    }
    if (m_batch.isEmpty()) {
        setState("ready");
        return;
    }
    m_probe.start(m_opt.player, QStringList() << "--probe" << m_batch);
}

void MediaController::onProbeFinished()
{
    const QString out = QString::fromUtf8(m_probe.readAllStandardOutput());
    for (const QString &line : out.split('\n'))
        if (line.startsWith("PROBE\t"))
            applyProbeLine(line);
    // anything the probe did not answer for (it crashed, or is missing)
    QVector<MediaFile> &files = m_model.files();
    for (int i = 0; i < files.size(); ++i) {
        MediaFile &f = files[i];
        if (!f.probed && m_batch.contains(m_opt.root + "/" + f.rel)) {
            f.probed = true;
            f.decode = "unsupported";
            f.reason = "could not be examined";
            m_model.changed(i);
            ++m_probedCount;
        }
    }
    emit progressChanged();
    emit selectionChanged();
    probeNext();
}

void MediaController::applyProbeLine(const QString &line)
{
    QHash<QString, QString> kv;
    for (const QString &field : line.split('\t')) {
        const int eq = field.indexOf('=');
        if (eq > 0)
            kv.insert(field.left(eq), field.mid(eq + 1));
    }
    const QString file = kv.value("file");
    if (!file.startsWith(m_opt.root + "/"))
        return;
    const QString rel = file.mid(m_opt.root.size() + 1);
    QVector<MediaFile> &files = m_model.files();
    for (int i = 0; i < files.size(); ++i) {
        MediaFile &f = files[i];
        if (f.rel != rel || f.probed)
            continue;
        f.probed = true;
        f.decode = kv.value("decode");
        const QString reason = kv.value("reason");
        f.reason = f.decode == "unsupported" ? (reason == "-" ? QString("not supported") : reason) : QString();
        QStringList info;
        const QString size = kv.value("size");
        if (size != "0x0")
            info << size;
        if (f.kind == "video") {
            const QStringList fps = kv.value("fps").split('/');
            if (fps.size() == 2 && fps[0].toInt() > 0 && fps[1].toInt() > 0) {
                const double r = fps[0].toDouble() / fps[1].toDouble();
                info << QString::number(r, 'f', qFuzzyCompare(r, qRound(r) * 1.0) ? 0 : 2) + " fps";
            }
            const double d = kv.value("duration").toDouble();
            if (d > 0)
                info << duration(d);
        }
        const QString codec = codecName(kv.value("codec"));
        if (!codec.isEmpty())
            info << codec;
        f.info = info.join(QString::fromUtf8(" · "));
        QStringList notes;
        if (f.decode != "unsupported" && kv.value("slow") == "1")
            notes << "may play slowly";
        if (f.kind == "video" && kv.value("rotation").toInt() != 0 && f.decode != "unsupported")
            notes << "plays unrotated";
        f.note = notes.join(", ");
        if (!f.playable() && f.checked)
            f.checked = false;   // a saved item that cannot play any more
        m_model.changed(i);
        ++m_probedCount;
        return;
    }
}

int MediaController::checkedCount() const
{
    int n = 0;
    for (const MediaFile &f : m_model.constFiles())
        if (f.checked)
            ++n;
    return n;
}

void MediaController::toggle(int row)
{
    QVector<MediaFile> &files = m_model.files();
    if (row < 0 || row >= files.size() || !files[row].playable())
        return;
    files[row].checked = !files[row].checked;
    m_model.changedAll();   // the order numbers after it change too
    emit selectionChanged();
    setDirty(true);
}

void MediaController::selectAll(bool on)
{
    for (MediaFile &f : m_model.files())
        f.checked = on && f.playable();
    m_model.changedAll();
    emit selectionChanged();
    setDirty(true);
}

bool MediaController::move(int row, int delta)
{
    if (!m_model.move(row, delta))
        return false;
    m_model.changedAll();
    setDirty(true);
    return true;
}

void MediaController::setImageDuration(int s)
{
    s = qBound(kMinImageDuration, s, kMaxImageDuration);
    if (s == m_imageDuration)
        return;
    m_imageDuration = s;
    emit settingsChanged();
    setDirty(true);
}

void MediaController::setLoop(bool on)
{
    if (on == m_loop)
        return;
    m_loop = on;
    emit settingsChanged();
    setDirty(true);
}

void MediaController::setAutostart(bool on)
{
    if (on == m_autostart)
        return;
    m_autostart = on;
    emit settingsChanged();
    setDirty(true);
}

QByteArray MediaController::playlistJson() const
{
    QJsonObject o = m_loaded;
    QJsonArray items;
    for (const MediaFile &f : m_model.constFiles())
        if (f.checked)
            items.append(f.rel);
    o.insert("version", 1);
    o.insert("image_duration_s", m_imageDuration);
    o.insert("loop", m_loop);
    o.insert("autostart", m_autostart);
    o.insert("items", items);
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

// The stick is pulled right after Save: temp file, fsync, rename, fsync the directory
bool MediaController::writeSafely(const QString &path, const QByteArray &data, QString *error) const
{
    const QString dirPath = QFileInfo(path).absolutePath();
    const QString tmp = dirPath + "/." + QFileInfo(path).fileName() + ".tmp";
    const QByteArray tmpName = QFile::encodeName(tmp);
    int fd = ::open(tmpName.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    qint64 done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.constData() + done, size_t(data.size() - done));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            *error = QString::fromLocal8Bit(strerror(errno));
            ::close(fd);
            ::unlink(tmpName.constData());
            return false;
        }
        done += n;
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        *error = QString::fromLocal8Bit(strerror(errno));
        ::unlink(tmpName.constData());
        return false;
    }
    if (::rename(tmpName.constData(), QFile::encodeName(path).constData()) != 0) {
        *error = QString::fromLocal8Bit(strerror(errno));
        ::unlink(tmpName.constData());
        return false;
    }
    int dfd = ::open(QFile::encodeName(dirPath).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::syncfs(dfd);
        ::close(dfd);
    }
    return true;
}

bool MediaController::save()
{
    if (m_readOnly) {
        setMessageTone("The USB stick is read-only: the playlist cannot be saved", "bad");
        return false;
    }
    QString error;
    if (!writeSafely(m_opt.root + "/" + kPlaylistName, playlistJson(), &error)) {
        setMessageTone("Saving failed: " + error, "bad");
        return false;
    }
    setDirty(false);
    setMessageTone(QString("Playlist saved on the stick (%1)").arg(kPlaylistName), "ok");
    return true;
}

void MediaController::play()
{
    if (checkedCount() == 0)
        return;
    if (m_readOnly) {
        // played from a copy; the player resolves the items on the stick
        QString error;
        if (m_opt.tempPlaylist.isEmpty() || !writeSafely(m_opt.tempPlaylist, playlistJson(), &error)) {
            setMessageTone("Cannot start: " + (error.isEmpty() ? QString("no temporary playlist") : error), "bad");
            return;
        }
    } else if (m_dirty || !QFileInfo::exists(m_opt.root + "/" + kPlaylistName)) {
        if (!save())
            return;
    }
    QCoreApplication::exit(kPlayExitCode);
}

void MediaController::back()
{
    QCoreApplication::exit(0);
}

void MediaController::setDirty(bool d)
{
    if (d == m_dirty)
        return;
    m_dirty = d;
    emit dirtyChanged();
}

void MediaController::setState(const QString &s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged();
}

void MediaController::setMessageTone(const QString &m, const QString &tone)
{
    m_message = m;
    m_messageTone = tone;
    emit messageChanged();
}
