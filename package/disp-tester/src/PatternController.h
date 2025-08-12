#ifndef PATTERNCONTROLLER_H
#define PATTERNCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QVariantMap>
#include "NetworkInterface.h"
#include "PatternParameters.h"

class PatternController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPattern READ currentPattern NOTIFY currentPatternChanged)
    Q_PROPERTY(QColor customColor READ customColor NOTIFY customColorChanged)
    Q_PROPERTY(bool showCustomColor READ showCustomColor NOTIFY showCustomColorChanged)
    Q_PROPERTY(QVariantMap patternParams READ patternParams NOTIFY patternParamsChanged)

public:
    explicit PatternController(QObject *parent = nullptr);
    ~PatternController();

    QString currentPattern() const { return m_currentPattern; }
    QColor customColor() const { return m_customColor; }
    bool showCustomColor() const { return m_showCustomColor; }
    QVariantMap patternParams() const { return m_parameters.toVariantMap(); }

    bool startNetworkInterface(int port);

public slots:
    void nextPattern();
    void previousPattern();
    void setPattern(const QString &pattern);
    void setCustomColor(int r, int g, int b);
    QString getResolution();
    QString listPatterns();
    QString getNetworkInfo();

signals:
    void currentPatternChanged();
    void customColorChanged();
    void showCustomColorChanged();
    void patternParamsChanged();

private slots:
    void handleNetworkCommand(const QString &command);

private:
    QStringList m_patterns;
    QString m_currentPattern;
    int m_currentIndex;
    QColor m_customColor;
    bool m_showCustomColor;
    NetworkInterface *m_networkInterface;
    PatternParameters m_parameters;

    void updatePattern(const QString &pattern);
    bool setPatternParameter(const QString &pattern, const QString &param, const QStringList &values);
    QString getPatternParameter(const QString &pattern, const QString &param);
    void resetParameters();
};

#endif // PATTERNCONTROLLER_H
