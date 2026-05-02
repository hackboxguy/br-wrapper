#ifndef PATTERNCONTROLLER_H
#define PATTERNCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include "NetworkInterface.h"
#include "PatternParameters.h"

// Default network server port
#define DEFAULT_NETWORK_PORT 8082

class PatternController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPattern READ currentPattern NOTIFY currentPatternChanged)
    Q_PROPERTY(QColor customColor READ customColor NOTIFY customColorChanged)
    Q_PROPERTY(bool showCustomColor READ showCustomColor NOTIFY showCustomColorChanged)
    Q_PROPERTY(QVariantMap patternParams READ patternParams NOTIFY patternParamsChanged)
    Q_PROPERTY(QString metadataStatus READ metadataStatus NOTIFY metadataStatusChanged)
    Q_PROPERTY(QString networkInfo READ getNetworkInfo NOTIFY networkInfoChanged)
    Q_PROPERTY(QString metadataAlign READ getMetadataAlign NOTIFY metadataAlignChanged)
    Q_PROPERTY(int metadataFontSize READ getMetadataFontSize NOTIFY metadataFontSizeChanged)
    Q_PROPERTY(QColor metadataColor READ getMetadataColor NOTIFY metadataColorChanged)
    Q_PROPERTY(bool userInteractionEnabled READ getUserInteractionEnabled NOTIFY userInteractionEnabledChanged)
    Q_PROPERTY(bool childActionButtonVisible READ childActionButtonVisible NOTIFY childActionButtonChanged)
    Q_PROPERTY(bool childActionActive READ childActionActive NOTIFY childActionStateChanged)
    Q_PROPERTY(QString childActionStartText READ childActionStartText NOTIFY childActionButtonChanged)
    Q_PROPERTY(QString childActionStopText READ childActionStopText NOTIFY childActionButtonChanged)
    Q_PROPERTY(QColor childActionStartColor READ childActionStartColor NOTIFY childActionButtonChanged)
    Q_PROPERTY(QColor childActionStopColor READ childActionStopColor NOTIFY childActionButtonChanged)

public:
    explicit PatternController(QObject *parent = nullptr);
    ~PatternController();

    QString currentPattern() const { return m_currentPattern; }
    QColor customColor() const { return m_customColor; }
    bool showCustomColor() const { return m_showCustomColor; }
    QVariantMap patternParams() const { return m_parameters.toVariantMap(); }
    QString metadataStatus() const { return m_metadataStatus; }
    bool childActionButtonVisible() const { return m_childActionButtonVisible; }
    bool childActionActive() const { return m_childActionActive; }
    QString childActionStartText() const { return m_childActionStartText; }
    QString childActionStopText() const { return m_childActionStopText; }
    QColor childActionStartColor() const { return m_childActionStartColor; }
    QColor childActionStopColor() const { return m_childActionStopColor; }

    bool startNetworkInterface(int port);
    bool startChildScript(const QString &program, const QStringList &arguments);
    void configureChildActionButton(bool visible, const QString &startText, const QString &stopText,
                                    const QColor &startColor, const QColor &stopColor);
    void requestQuit(const QString &reason);

public slots:
    void nextPattern();
    void previousPattern();
    void setPattern(const QString &pattern);
    void setCustomColor(int r, int g, int b);
    QString getResolution();
    QString listPatterns();
    QString getNetworkInfo();
    QString getMetadataAlign() const { return m_metadataAlign; }
    int getMetadataFontSize() const { return m_metadataFontSize; }
    QColor getMetadataColor() const { return m_metadataColor; }
    bool getUserInteractionEnabled() const { return m_userInteractionEnabled; }
    void requestQuit();
    void toggleChildAction();

signals:
    void currentPatternChanged();
    void customColorChanged();
    void showCustomColorChanged();
    void patternParamsChanged();
    void metadataStatusChanged();
    void metadataTextChanged();
    void networkInfoChanged();
    void metadataAlignChanged();
    void metadataFontSizeChanged();
    void metadataColorChanged();
    void userInteractionEnabledChanged();
    void childActionButtonChanged();
    void childActionStateChanged();

private slots:
    void handleNetworkCommand(const QString &command);
    void handleChildFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleChildError(QProcess::ProcessError error);
    void forceStopChildScript();

private:
    QStringList m_patterns;
    QString m_currentPattern;
    int m_currentIndex;
    QColor m_customColor;
    bool m_showCustomColor;
    NetworkInterface *m_networkInterface;
    QProcess *m_childProcess;
    QTimer *m_childShutdownTimer;
    int m_networkPort;
    bool m_shutdownRequested;
    PatternParameters m_parameters;

    // Metadata management
    QString m_metadataStatus;  // "autohide", "enable", "disable"
    QString m_metadataText;    // Custom metadata text (empty = use default IP:port)
    QString m_metadataAlign;   // "left", "center", "right"
    int m_metadataFontSize;    // Font size (8-48)
    QColor m_metadataColor;    // Text color
    bool m_userInteractionEnabled; // Enable/disable user touch interaction
    bool m_childActionButtonVisible;
    bool m_childActionActive;
    QString m_childActionStartText;
    QString m_childActionStopText;
    QColor m_childActionStartColor;
    QColor m_childActionStopColor;

    void updatePattern(const QString &pattern);
    bool setPatternParameter(const QString &pattern, const QString &param, const QStringList &values);
    QString getPatternParameter(const QString &pattern, const QString &param);
    void resetParameters();

    // Metadata methods
    void setMetadataStatus(const QString &status);
    void setMetadataText(const QString &text);
    QString getMetadataText() const;
    void clearMetadataText();
    void setMetadataAlign(const QString &align);
    void setMetadataFontSize(int size);
    void setMetadataColor(const QColor &color);
    void setMetadataColor(int r, int g, int b);
    void setMetadataColorByName(const QString &colorName);
    void setUserInteractionEnabled(bool enabled);
    bool isChildScriptRunning() const;
    void stopChildScript();
    void finishApplicationQuit();
    void cleanupChildProcess();
    bool sendChildControlCommand(const QString &command, bool enabled);
    void setChildActionActive(bool active);
};

#endif // PATTERNCONTROLLER_H
