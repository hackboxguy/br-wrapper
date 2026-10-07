#include "OnOffState.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSaveFile>

OnOffState::OnOffState(const QString &name, bool enabled, const QString &stateFile, QObject *parent)
    : QObject(parent), m_name(name), m_enabled(enabled), m_stateFile(stateFile)
{
    watch();
}

// The file and its directory: a writer replaces the file (temp + rename),
// which ends a watch on the file itself, and creates it where it was missing
void OnOffState::watch()
{
    if (m_stateFile.isEmpty())
        return;
    m_watcher = new QFileSystemWatcher(this);
    const QString dir = QFileInfo(m_stateFile).absolutePath();
    if (QFileInfo(dir).isDir())
        m_watcher->addPath(dir);
    if (QFileInfo::exists(m_stateFile))
        m_watcher->addPath(m_stateFile);
    auto changed = [this]() {
        if (QFileInfo::exists(m_stateFile) && !m_watcher->files().contains(m_stateFile))
            m_watcher->addPath(m_stateFile);
        reload();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
}

bool OnOffState::reload()
{
    QFile file(m_stateFile);
    if (m_stateFile.isEmpty() || !file.open(QIODevice::ReadOnly))
        return false;
    const QString word = QString::fromUtf8(file.readAll()).trimmed().toLower();
    bool enabled;
    if (word == QLatin1String("on"))
        enabled = true;
    else if (word == QLatin1String("off"))
        enabled = false;
    else {
        // empty: a writer between truncate and write (not ours: ours replace)
        if (!word.isEmpty() && word != m_lastBadWord) {
            qWarning() << m_name << ": ignoring" << word << "in" << m_stateFile;
            m_lastBadWord = word;
        }
        return false;
    }
    m_lastBadWord.clear();
    if (enabled == m_enabled)
        return false;               // our own write, or nothing new
    qInfo() << m_name << ":" << (enabled ? "on" : "off") << "from" << m_stateFile;
    m_enabled = enabled;
    emit changed();
    return true;
}

void OnOffState::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    emit changed();
    save();
}

// Whole file or nothing, as the DMS panel's state; a file that cannot be
// written only costs the memory of the choice, never the switch itself.
void OnOffState::save() const
{
    if (m_stateFile.isEmpty())
        return;
    QSaveFile file(m_stateFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
            || file.write(m_enabled ? "on\n" : "off\n") < 0
            || !file.commit()) {
        qWarning() << m_name << ": could not save the state to" << m_stateFile
                   << "-" << file.errorString();
    }
}
