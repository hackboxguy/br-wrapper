#pragma once

#include <QObject>
#include <QString>

// A switch of the control row that a launcher can remember: the map behind
// every theme (MAP, and the B key), the telltales' ghost of an unlit lamp
// (T) and the info bar (B). Each starts from its option (--map-backdrop,
// --telltale-min-dark-level, --info-bar); with a state file
// (--map-backdrop-state=, --telltale-min-dark-level-state=,
// --info-bar-state=) every change is written there as on or off, whole or
// not at all, so a launcher can start the next run the way this one was
// left (br-wrapper's cluster-v2.sh). Without one the choice lasts for the
// run (the stand-alone board).
class OnOffState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)

public:
    explicit OnOffState(const QString &name, bool enabled = true,
                        const QString &stateFile = QString(), QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    Q_INVOKABLE void toggle() { setEnabled(!m_enabled); }
    QString stateFile() const { return m_stateFile; }

signals:
    void changed();

private:
    void save() const;

    QString m_name;
    bool m_enabled;
    QString m_stateFile;
};
