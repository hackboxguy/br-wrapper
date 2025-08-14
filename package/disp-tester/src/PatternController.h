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
    Q_PROPERTY(QString metadataStatus READ metadataStatus NOTIFY metadataStatusChanged)
    Q_PROPERTY(QString networkInfo READ getNetworkInfo NOTIFY networkInfoChanged)

public:
    explicit PatternController(QObject *parent = nullptr);
    ~PatternController();

    QString currentPattern() const { return m_currentPattern; }
    QColor customColor() const { return m_customColor; }
    bool showCustomColor() const { return m_showCustomColor; }
    QVariantMap patternParams() const { return m_parameters.toVariantMap(); }
    QString metadataStatus() const { return m_metadataStatus; }

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
    void metadataStatusChanged();
    void metadataTextChanged();
    void networkInfoChanged();

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

    // Metadata management
    QString m_metadataStatus;  // "autohide", "enable", "disable"
    QString m_metadataText;    // Custom metadata text (empty = use default IP:port)

    void updatePattern(const QString &pattern);
    bool setPatternParameter(const QString &pattern, const QString &param, const QStringList &values);
    QString getPatternParameter(const QString &pattern, const QString &param);
    void resetParameters();

    // Metadata methods
    void setMetadataStatus(const QString &status);
    void setMetadataText(const QString &text);
    QString getMetadataText() const;
    void clearMetadataText();
};

#endif // PATTERNCONTROLLER_H
