#ifndef PATTERNCONTROLLER_H
#define PATTERNCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include "NetworkInterface.h"

class PatternController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPattern READ currentPattern NOTIFY currentPatternChanged)
    Q_PROPERTY(QColor customColor READ customColor NOTIFY customColorChanged)
    Q_PROPERTY(bool showCustomColor READ showCustomColor NOTIFY showCustomColorChanged)

public:
    explicit PatternController(QObject *parent = nullptr);
    ~PatternController();

    QString currentPattern() const { return m_currentPattern; }
    QColor customColor() const { return m_customColor; }
    bool showCustomColor() const { return m_showCustomColor; }

    bool startNetworkInterface(int port);

public slots:
    void nextPattern();
    void setPattern(const QString &pattern);
    void setCustomColor(int r, int g, int b);
    QString getResolution();

signals:
    void currentPatternChanged();
    void customColorChanged();
    void showCustomColorChanged();

private slots:
    void handleNetworkCommand(const QString &command);

private:
    QStringList m_patterns;
    QString m_currentPattern;
    int m_currentIndex;
    QColor m_customColor;
    bool m_showCustomColor;
    NetworkInterface *m_networkInterface;

    void updatePattern(const QString &pattern);
};

#endif // PATTERNCONTROLLER_H
