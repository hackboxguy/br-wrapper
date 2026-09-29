#ifndef SYSTEMIMAGETEXT_H
#define SYSTEMIMAGETEXT_H

// What the System image section says, as plain functions of the engine's
// files, so the wording can be tested without a window or a device
// (tests/test_outcome_text.cpp).

#include <QFile>
#include <QString>

namespace SystemImageText {

// key=value lines, the format every file of the engine uses. The first line
// with the key wins; unknown keys are ignored, which is what lets the engine
// add one (refused_reason=) without breaking this reader.
inline QString fileValue(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    const QByteArray prefix = key.toUtf8() + '=';
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.startsWith(prefix)) return QString::fromUtf8(line.mid(prefix.size()));
    }
    return QString();
}

// The rolled-back line. `reason` is the commit service's own words for why it
// refused the candidate (refused_reason= in the engine's public status, from
// pi-ab-update 2.06 on), e.g. "health lost in the settle window: health unit
// qt-demo-launcher.service is not active". A candidate that hung or lost power
// never reached the service and has none: the line is then as it always was.
inline QString fallbackText(const QString &what, const QString &running, const QString &reason)
{
    QString text = QString("The update to %1 did not pass its health check; running %2 again")
                       .arg(what, running);
    const QString why = reason.trimmed();
    if (!why.isEmpty()) text += QString(". Reason: %1").arg(why);
    return text;
}

} // namespace SystemImageText

#endif // SYSTEMIMAGETEXT_H
