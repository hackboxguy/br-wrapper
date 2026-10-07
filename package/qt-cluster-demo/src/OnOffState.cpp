#include "OnOffState.h"

#include <QDebug>
#include <QSaveFile>

OnOffState::OnOffState(const QString &name, bool enabled, const QString &stateFile, QObject *parent)
    : QObject(parent), m_name(name), m_enabled(enabled), m_stateFile(stateFile)
{
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
