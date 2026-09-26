#include "config.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>
#include <QtWidgets/QMessageBox>
#include <QProcess>
#include <QDir>
#include <QFont>
#include <QDesktopWidget>
#include <QStyleFactory>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QPixmap>
#include <QIcon>
#include <QFileSystemWatcher>
#include <QScreen>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QImageReader>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QPointer>
#include <cmath>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "NetworkInterface.h"

struct ButtonConfig {
    QString id;
    bool enabled = true;
    bool visible = true;
    QString text;
    QString iconPath;
    QString program;
    QStringList arguments;
    QString workingDirectory = "/tmp";
    QString action; // "quit" for special actions
    QString page = "home";
    QString targetPage;
    QString pageTitle;
    bool detached = false;

    // Position and size
    int row = 0;
    int column = 0;
    int columnSpan = 1;
    int rowSpan = 1;
    int width = 200;
    int height = 100;

    // Icon settings
    int iconWidth = 48;
    int iconHeight = 48;
    QString iconLayout = "icon_left"; // icon_left, icon_right, icon_top, icon_only, text_only

    // Styling
    int fontSize = 24;
    QString subtitle;     // "tiles" theme: second line under the text
    QString accentColor;  // "tiles" theme: badge/stripe color (defaults to background_color)
    QString backgroundColor = "#404040";
    QString hoverColor = "#505050";
    int borderRadius = 15;
};

struct TitleConfig {
    QString text = "Touch Applications";
    QString subtitle;  // "tiles" theme only
    QString logoPath;
    int logoWidth = 150;
    int logoHeight = 60;
    QString layout = "text_only"; // logo_left, logo_right, logo_top, logo_only, text_only
    int fontSize = 32;
    QString color = "#ffffff";
};

struct LayoutConfig {
    QString type = "vertical"; // grid, vertical, horizontal
    int columns = 2;
    int rows = 2;
    int spacing = 15;
    int topMargin = 50;
    int bottomMargin = 50;
    int leftMargin = 50;
    int rightMargin = 50;
    bool dynamicLayout = true;  // Dynamic sizing to fill screen (can be disabled)
};

// Optional "theme" block. The default "classic" style keeps the original
// stylesheet buttons; "tiles" switches to the painted card look below.
// Default colors are multiples of the RGB565 steps so they don't dither on
// 16bpp framebuffers.
struct ThemeConfig {
    QString style = "classic"; // classic, tiles
    QString fontFamily;
    QString backgroundColor = "#080C18";
    QString gridColor = "#101828";
    int gridSpacing = 48;      // 0 disables the background grid
    bool cornerMarks = true;   // registration marks on the outermost pixels
    bool colorBars = true;     // SMPTE bar strip under the header
    QString cardColor = "#182030";
    QString cardHoverColor = "#202C40";
    QString cardBorderColor = "#283450";
    QString textColor = "#F0F4F8";
    QString subtextColor = "#8894A8";
    bool showClock = true;
    bool showIp = true;
    bool showResolution = true;
    bool animations = true;

    bool isTiles() const { return style == "tiles"; }
};

struct LauncherConfig {
    ThemeConfig theme;
    TitleConfig title;
    LayoutConfig layout;
    QList<ButtonConfig> buttons;
    int windowWidth = 800;
    int windowHeight = 600;
};

// ---------------------------------------------------------------------------
// "tiles" theme widgets. None of these need moc: they only use lambdas.
// ---------------------------------------------------------------------------

static QColor mixColor(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

static QFont themeFont(const ThemeConfig &theme, int pixelSize, int weight)
{
    QFont font;
    if (!theme.fontFamily.isEmpty()) {
        font.setFamily(theme.fontFamily);
    }
    font.setPixelSize(qMax(8, pixelSize));
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

// Loads a PNG or SVG (via the qsvg image plugin) fitted into box. SVGs are
// rasterized at the target size so they stay sharp. A valid tint recolors
// every opaque pixel while keeping the icon's alpha, so one white line-art
// icon set serves every accent color.
static QPixmap loadIcon(const QString &path, const QSize &box, const QColor &tint = QColor())
{
    if (path.isEmpty() || box.isEmpty() || !QFile::exists(path)) {
        return QPixmap();
    }

    QImageReader reader(path);
    QSize natural = reader.size();
    if (natural.isValid()) {
        reader.setScaledSize(natural.scaled(box, Qt::KeepAspectRatio));
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return QPixmap();
    }
    if (image.size() != image.size().scaled(box, Qt::KeepAspectRatio) || !natural.isValid()) {
        image = image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (tint.isValid()) {
        QPainter p(&image);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(image.rect(), tint);
    }
    return QPixmap::fromImage(image);
}

// First IPv4 address of an interface that is up and not loopback.
static QString primaryIPv4()
{
    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) {
        return QString();
    }

    QString result;
    for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;

        char buf[INET_ADDRSTRLEN];
        const struct sockaddr_in *sin = reinterpret_cast<const struct sockaddr_in *>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
            result = QString::fromLatin1(buf);
            break;
        }
    }
    freeifaddrs(list);
    return result;
}

// Central widget. Paints nothing in the classic style, so the main window's
// black stylesheet background shows through as before.
class BackdropWidget : public QWidget
{
public:
    explicit BackdropWidget(const ThemeConfig *theme, QWidget *parent = nullptr)
        : QWidget(parent), m_theme(theme) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_theme || !m_theme->isTiles()) return;

        QPainter p(this);
        p.fillRect(rect(), QColor(m_theme->backgroundColor));

        if (m_theme->gridSpacing > 0) {
            p.setPen(QColor(m_theme->gridColor));
            int step = m_theme->gridSpacing;
            // Center the grid so the margins on both sides match
            int ox = (width() % step) / 2;
            int oy = (height() % step) / 2;
            for (int x = ox; x < width(); x += step) p.drawLine(x, 0, x, height());
            for (int y = oy; y < height(); y += step) p.drawLine(0, y, width(), y);
        }

        if (m_theme->cornerMarks) {
            // Drawn on the outermost pixel rows/columns: if any mark is
            // missing, the panel is cropping or mis-timing the frame.
            int len = qMax(16, height() / 30);
            int w = width() - 1, h = height() - 1;
            p.setPen(QPen(QColor("#5868A0"), 2, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(0, 1, len, 1);      p.drawLine(1, 0, 1, len);
            p.drawLine(w - len, 1, w, 1);  p.drawLine(w, 0, w, len);
            p.drawLine(0, h, len, h);      p.drawLine(1, h - len, 1, h);
            p.drawLine(w - len, h, w, h);  p.drawLine(w, h - len, w, h);
        }
    }

private:
    const ThemeConfig *m_theme;
};

// Header: logo, title/breadcrumb, status chips, a seconds clock with a live
// pulse (doubles as a frozen-frame indicator) and the SMPTE bar strip with a
// scanline sweeping across it.
class LauncherHeader : public QWidget
{
public:
    LauncherHeader(const ThemeConfig &theme, const QString &title, const QString &subtitle,
                   const QString &logoPath, int port, int height, QWidget *parent = nullptr)
        : QWidget(parent), m_theme(theme), m_title(title), m_subtitle(subtitle),
          m_logoPath(logoPath), m_port(port)
    {
        setFixedHeight(height);
        setAttribute(Qt::WA_OpaquePaintEvent, false);

        m_ip = primaryIPv4();
        if (QScreen *screen = QApplication::primaryScreen()) {
            m_resolution = QString("%1×%2").arg(screen->size().width()).arg(screen->size().height());
        }

        m_tick = new QTimer(this);
        m_tick->setInterval(1000);
        QObject::connect(m_tick, &QTimer::timeout, [this]() {
            // DHCP may come up after the launcher, so keep polling
            if (++m_tickCount % 5 == 0) {
                QString ip = primaryIPv4();
                if (ip != m_ip) {
                    m_ip = ip;
                    update();
                    return;
                }
            }
            update(m_clockRect.toAlignedRect().adjusted(-2, -2, 2, 2));
        });

        m_anim = new QTimer(this);
        m_anim->setInterval(40);
        QObject::connect(m_anim, &QTimer::timeout, [this]() {
            update(m_pulseRect.toAlignedRect().adjusted(-2, -2, 2, 2));
            if (m_scanActive || scanPhase() >= 0) {
                update(m_stripRect.toAlignedRect());
            }
        });
        m_elapsed.start();
    }

protected:
    void showEvent(QShowEvent *) override
    {
        if (m_theme.showClock) m_tick->start();
        if (m_theme.animations) m_anim->start();
    }

    void hideEvent(QHideEvent *) override
    {
        m_tick->stop();
        m_anim->stop();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const qreal W = width();
        const qreal H = height();
        const QColor text(m_theme.textColor);
        const QColor subtext(m_theme.subtextColor);

        qreal stripH = m_theme.colorBars ? qMax(4.0, qRound(H * 0.06) * 1.0) : 0;
        qreal bodyH = H - stripH - (stripH > 0 ? qRound(H * 0.14) : 0);

        // Logo
        qreal x = 0;
        if (!m_logoPath.isEmpty()) {
            QSize box(qRound(bodyH * 0.98), qRound(bodyH * 0.74));
            if (m_logo.isNull() || m_logoBox != box) {
                m_logo = loadIcon(m_logoPath, box);
                m_logoBox = box;
            }
            if (!m_logo.isNull()) {
                p.drawPixmap(QPointF(0, (bodyH - m_logo.height()) / 2), m_logo);
                x = m_logo.width() + H * 0.2;
            }
        }

        // Title block
        QFont titleFont = themeFont(m_theme, qRound(bodyH * 0.38), QFont::Bold);
        QFont subFont = themeFont(m_theme, qRound(bodyH * 0.2), QFont::Normal);
        QFontMetricsF fmT(titleFont), fmS(subFont);
        qreal gap = bodyH * 0.05;
        qreal blockH = fmT.height() + (m_subtitle.isEmpty() ? 0 : gap + fmS.height());
        qreal top = (bodyH - blockH) / 2;
        p.setFont(titleFont);
        p.setPen(text);
        p.drawText(QPointF(x, top + fmT.ascent()), m_title);
        qreal titleRight = x + fmT.horizontalAdvance(m_title);
        if (!m_subtitle.isEmpty()) {
            p.setFont(subFont);
            p.setPen(subtext);
            p.drawText(QPointF(x, top + fmT.height() + gap + fmS.ascent()), m_subtitle);
            titleRight = qMax(titleRight, x + fmS.horizontalAdvance(m_subtitle));
        }
        titleRight += H * 0.4;

        // Clock + live pulse
        qreal right = W;
        if (m_theme.showClock) {
            QFont clockFont = themeFont(m_theme, qRound(bodyH * 0.36), QFont::Medium);
            QFont dateFont = themeFont(m_theme, qRound(bodyH * 0.19), QFont::Normal);
            QFontMetricsF fmC(clockFont), fmD(dateFont);
            QDateTime now = QDateTime::currentDateTime();
            qreal clockW = qMax(fmC.horizontalAdvance("88:88:88"), fmD.horizontalAdvance(now.toString("ddd dd MMM yyyy")));
            qreal cBlockH = fmC.height() + gap + fmD.height();
            qreal cTop = (bodyH - cBlockH) / 2;
            m_clockRect = QRectF(W - clockW, cTop, clockW, cBlockH);
            p.setFont(clockFont);
            p.setPen(text);
            p.drawText(QRectF(W - clockW, cTop, clockW, fmC.height()), Qt::AlignRight | Qt::AlignVCenter,
                       now.toString("HH:mm:ss"));
            p.setFont(dateFont);
            p.setPen(subtext);
            p.drawText(QRectF(W - clockW, cTop + fmC.height() + gap, clockW, fmD.height()),
                       Qt::AlignRight | Qt::AlignVCenter, now.toString("ddd dd MMM yyyy"));

            qreal r = qMax(3.0, bodyH * 0.06);
            QPointF c(W - clockW - H * 0.22, cTop + fmC.height() / 2);
            m_pulseRect = QRectF(c.x() - r * 3, c.y() - r * 3, r * 6, r * 6);
            QColor live("#34D399");
            if (m_theme.animations) {
                qreal t = std::fmod(m_elapsed.elapsed() / 1600.0, 1.0);
                QColor halo = live;
                halo.setAlphaF(0.55 * (1.0 - t));
                p.setPen(Qt::NoPen);
                p.setBrush(halo);
                p.drawEllipse(c, r * (1.0 + 1.9 * t), r * (1.0 + 1.9 * t));
            }
            p.setPen(Qt::NoPen);
            p.setBrush(live);
            p.drawEllipse(c, r, r);
            right = m_pulseRect.left() - H * 0.15;
        }

        // Status chips, dropped from the left if they would hit the title
        QList<QPair<QString, QString>> chips;
        if (m_theme.showResolution && !m_resolution.isEmpty()) chips << qMakePair(QString("RES"), m_resolution);
        if (m_theme.showIp) chips << qMakePair(QString("IP"), m_ip.isEmpty() ? QString("no link") : m_ip);
        if (m_theme.showIp && m_port > 0) chips << qMakePair(QString("API"), QString(":%1").arg(m_port));

        QFont labelFont = themeFont(m_theme, qRound(bodyH * 0.16), QFont::Bold);
        QFont valueFont = themeFont(m_theme, qRound(bodyH * 0.2), QFont::Medium);
        QFontMetricsF fmL(labelFont), fmV(valueFont);
        qreal chipH = bodyH * 0.44;
        qreal pad = chipH * 0.42;
        for (int i = chips.size() - 1; i >= 0; --i) {
            qreal labelW = fmL.horizontalAdvance(chips[i].first);
            qreal valueW = fmV.horizontalAdvance(chips[i].second);
            qreal chipW = pad + labelW + pad * 0.6 + valueW + pad;
            qreal left = right - chipW;
            if (left < titleRight) break;
            QRectF chip(left, (bodyH - chipH) / 2, chipW, chipH);
            p.setPen(QPen(QColor(m_theme.cardBorderColor), 1.2));
            p.setBrush(QColor(m_theme.cardColor));
            p.drawRoundedRect(chip, chipH / 2, chipH / 2);
            p.setFont(labelFont);
            p.setPen(subtext);
            p.drawText(QRectF(chip.left() + pad, chip.top(), labelW, chipH), Qt::AlignVCenter, chips[i].first);
            p.setFont(valueFont);
            p.setPen(text);
            p.drawText(QRectF(chip.left() + pad + labelW + pad * 0.6, chip.top(), valueW + 1, chipH),
                       Qt::AlignVCenter, chips[i].second);
            right = left - H * 0.12;
        }

        // SMPTE 75% bars
        if (stripH > 0) {
            m_stripRect = QRectF(0, H - stripH, W, stripH);
            static const char *bars[] = { "#C0C0C0", "#C0C000", "#00C0C0", "#00C000",
                                          "#C000C0", "#C00000", "#0000C0" };
            QPainterPath clip;
            clip.addRoundedRect(m_stripRect, stripH / 2, stripH / 2);
            p.save();
            p.setClipPath(clip);
            qreal segW = W / 7.0;
            for (int i = 0; i < 7; ++i) {
                p.fillRect(QRectF(i * segW, m_stripRect.top(), segW + 1, stripH), QColor(bars[i]));
            }
            qreal phase = m_theme.animations ? scanPhase() : -1;
            m_scanActive = phase >= 0;
            if (m_scanActive) {
                qreal bandW = W * 0.14;
                qreal cx = -bandW + phase * (W + 2 * bandW);
                QLinearGradient g(cx - bandW / 2, 0, cx + bandW / 2, 0);
                g.setColorAt(0.0, QColor(255, 255, 255, 0));
                g.setColorAt(0.5, QColor(255, 255, 255, 210));
                g.setColorAt(1.0, QColor(255, 255, 255, 0));
                p.fillRect(QRectF(cx - bandW / 2, m_stripRect.top(), bandW, stripH), g);
            }
            p.restore();
        }
    }

private:
    // 0..1 while the scanline sweeps (2.2 s), -1 during the 3.8 s pause
    qreal scanPhase() const
    {
        qint64 t = m_elapsed.elapsed() % 6000;
        return t < 2200 ? t / 2200.0 : -1;
    }

    ThemeConfig m_theme;
    QString m_title;
    QString m_subtitle;
    QString m_logoPath;
    QPixmap m_logo;
    QSize m_logoBox;
    int m_port;
    QString m_ip;
    QString m_resolution;
    QTimer *m_tick;
    QTimer *m_anim;
    int m_tickCount = 0;
    QElapsedTimer m_elapsed;
    QRectF m_clockRect;
    QRectF m_pulseRect;
    QRectF m_stripRect;
    bool m_scanActive = false;
};

// Card-style launcher button. Still a QPushButton so layouts, the pressed()
// connection and qobject_cast in buttonClicked() keep working.
class TileButton : public QPushButton
{
public:
    TileButton(const ButtonConfig &config, const ThemeConfig &theme, bool missing,
               QWidget *parent = nullptr)
        : QPushButton(parent), m_config(config), m_theme(theme), m_missing(missing)
    {
        setText(config.text);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        m_accent = QColor(config.accentColor.isEmpty() ? config.backgroundColor : config.accentColor);
        if (!m_accent.isValid()) m_accent = QColor("#38BDF8");

        m_revealAnim = new QVariantAnimation(this);
        m_revealAnim->setStartValue(0.0);
        m_revealAnim->setEndValue(1.0);
        m_revealAnim->setDuration(420);
        m_revealAnim->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(m_revealAnim, &QVariantAnimation::valueChanged, [this](const QVariant &v) {
            m_reveal = v.toReal();
            update();
        });

        m_rippleAnim = new QVariantAnimation(this);
        m_rippleAnim->setStartValue(0.0);
        m_rippleAnim->setEndValue(1.0);
        m_rippleAnim->setDuration(480);
        m_rippleAnim->setEasingCurve(QEasingCurve::OutQuad);
        QObject::connect(m_rippleAnim, &QVariantAnimation::valueChanged, [this](const QVariant &v) {
            m_ripple = v.toReal();
            update();
        });
        QObject::connect(m_rippleAnim, &QVariantAnimation::finished, [this]() {
            m_ripple = -1;
            update();
        });
    }

    int gridRow() const { return m_config.row; }
    int gridColumn() const { return m_config.column; }

    void playReveal(int delayMs)
    {
        if (!m_theme.animations) return;
        m_revealAnim->stop();
        m_reveal = 0.0;
        update();
        QTimer::singleShot(delayMs, this, [this]() { m_revealAnim->start(); });
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_theme.animations) {
            m_ripplePos = event->localPos();
            m_rippleAnim->stop();
            m_ripple = 0.0;
            m_rippleAnim->start();
        }
        QPushButton::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal h = r.height();
        // Size unit: the height, but capped by the width so a tall tile
        // (e.g. 3 rows on 1080 lines) doesn't grow text past its width
        const qreal u = qMin(h, r.width() / 3.6);

        // Entrance: fade in while growing from 92%
        if (m_reveal < 1.0) {
            qreal s = 0.92 + 0.08 * m_reveal;
            p.translate(r.center());
            p.scale(s, s);
            p.translate(-r.center());
            p.setOpacity(m_reveal);
        }
        if (m_missing) p.setOpacity(p.opacity() * 0.5);

        const QColor text(m_theme.textColor);
        const QColor subtext(m_theme.subtextColor);
        const bool down = isDown();
        const bool hover = underMouse();
        const qreal radius = qMin(u * 0.14, 28.0);

        QPainterPath card;
        card.addRoundedRect(r, radius, radius);
        QColor fill = down ? mixColor(QColor(m_theme.cardColor), m_accent, 0.22)
                           : hover ? QColor(m_theme.cardHoverColor) : QColor(m_theme.cardColor);
        p.fillPath(card, fill);

        p.save();
        p.setClipPath(card);
        // Accent stripe and a faint top sheen
        qreal barW = qMax(4.0, u * 0.035);
        p.fillRect(QRectF(r.left(), r.top(), barW, h), m_accent);
        p.fillRect(QRectF(r.left(), r.top(), r.width(), qMax(1.0, u * 0.008)), QColor(255, 255, 255, 18));
        if (m_ripple >= 0) {
            qreal maxR = std::hypot(qMax(m_ripplePos.x(), r.width() - m_ripplePos.x()),
                                    qMax(m_ripplePos.y(), r.height() - m_ripplePos.y()));
            QColor wave = m_accent;
            wave.setAlphaF(0.35 * (1.0 - m_ripple));
            p.setPen(Qt::NoPen);
            p.setBrush(wave);
            p.drawEllipse(m_ripplePos, maxR * m_ripple, maxR * m_ripple);
        }
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(down ? m_accent : QColor(m_theme.cardBorderColor), down ? 2.0 : 1.2));
        p.drawPath(card);

        // Icon badge
        qreal x = r.left() + barW + u * 0.2;
        bool hasIcon = !m_config.iconPath.isEmpty() && m_config.iconLayout != "text_only";
        if (hasIcon) {
            qreal badge = u * 0.56;
            QRectF badgeRect(x, r.center().y() - badge / 2, badge, badge);
            QColor badgeFill = m_accent;
            badgeFill.setAlphaF(down ? 0.32 : 0.16);
            QColor badgeEdge = m_accent;
            badgeEdge.setAlphaF(0.45);
            p.setPen(QPen(badgeEdge, 1.2));
            p.setBrush(badgeFill);
            p.drawRoundedRect(badgeRect, badge * 0.26, badge * 0.26);

            QSize iconBox(qRound(badge * 0.62), qRound(badge * 0.62));
            if (m_icon.isNull() || m_iconBox != iconBox) {
                m_icon = loadIcon(m_config.iconPath, iconBox, m_accent.lighter(115));
                m_iconBox = iconBox;
            }
            if (!m_icon.isNull()) {
                p.drawPixmap(QPointF(badgeRect.center().x() - m_icon.width() / 2.0,
                                     badgeRect.center().y() - m_icon.height() / 2.0), m_icon);
            }
            x = badgeRect.right() + u * 0.17;
        }

        // Chevron for buttons that open a page
        qreal textRight = r.right() - u * 0.18;
        if (m_config.action.toLower() == "navigate") {
            qreal s = u * 0.09;
            QPointF c(r.right() - u * 0.22, r.center().y());
            p.setPen(QPen(m_accent, qMax(2.0, u * 0.025), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(QPolygonF() << QPointF(c.x() - s / 2, c.y() - s)
                                       << QPointF(c.x() + s / 2, c.y())
                                       << QPointF(c.x() - s / 2, c.y() + s));
            textRight = c.x() - u * 0.2;
        }
        qreal textW = qMax(10.0, textRight - x);

        // Title shrinks (to 70%) before it elides; subtitle just elides
        int titlePx = qRound(u * 0.19);
        QFont titleFont = themeFont(m_theme, titlePx, QFont::DemiBold);
        while (titlePx > u * 0.19 * 0.7 && QFontMetricsF(titleFont).horizontalAdvance(m_config.text) > textW) {
            titleFont.setPixelSize(--titlePx);
        }
        QFontMetricsF fmT(titleFont);
        QString title = fmT.elidedText(m_config.text, Qt::ElideRight, textW);

        QString sub = m_missing ? QString("Not installed") : m_config.subtitle;
        QFont subFont = themeFont(m_theme, qRound(u * 0.12), QFont::Normal);
        QFontMetricsF fmS(subFont);
        sub = fmS.elidedText(sub, Qt::ElideRight, textW);

        qreal gap = u * 0.04;
        qreal blockH = fmT.height() + (sub.isEmpty() ? 0 : gap + fmS.height());
        qreal top = r.center().y() - blockH / 2;
        p.setFont(titleFont);
        p.setPen(text);
        p.drawText(QPointF(x, top + fmT.ascent()), title);
        if (!sub.isEmpty()) {
            p.setFont(subFont);
            p.setPen(m_missing ? QColor("#F87171") : subtext);
            p.drawText(QPointF(x, top + fmT.height() + gap + fmS.ascent()), sub);
        }
    }

private:
    ButtonConfig m_config;
    ThemeConfig m_theme;
    bool m_missing;
    QColor m_accent;
    QPixmap m_icon;
    QSize m_iconBox;
    qreal m_reveal = 1.0;
    qreal m_ripple = -1;
    QPointF m_ripplePos;
    QVariantAnimation *m_revealAnim;
    QVariantAnimation *m_rippleAnim;
};

class TouchAppLauncher : public QMainWindow
{
    Q_OBJECT

public:
    TouchAppLauncher(const QString &configPath = QString(), int networkPort = DEFAULT_LAUNCHER_PORT,
                     bool forceFixedLayout = false, QWidget *parent = nullptr)
      : QMainWindow(parent), m_configWatcher(nullptr), m_currentConfigFile(""),
        m_configPath(configPath), m_networkPort(networkPort),
        m_networkInterface(nullptr), m_runningProcess(nullptr), m_runningAppId(""),
        m_currentPage("home"),
        m_scaleFactor(1.0), m_forceFixedLayout(forceFixedLayout),
        m_dynamicButtonWidth(0), m_dynamicButtonHeight(0)
    {
        loadConfig();
        calculateScaleFactor();
        calculateDynamicSizes();
        setupUI();
        validatePrograms();
        setupNetworkInterface();
        setupConfigWatcher();
    }

private slots:
    void buttonClicked()
    {
        QPushButton *button = qobject_cast<QPushButton*>(sender());
        if (!button) return;

        activateButton(button->property("buttonId").toString());
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QMainWindow::showEvent(event);
        // Replay the entrance each time we come back from a launched app
        animateTilesIn();
    }

private:
    void activateButton(const QString &buttonId)
    {
        // Find button config
        ButtonConfig config;
        bool found = false;
        for (const auto &btn : m_config.buttons) {
            if (btn.id == buttonId) {
                config = btn;
                found = true;
                break;
            }
        }

        if (!found) {
            qWarning() << "Button config not found for ID:" << buttonId;
            return;
        }

        // Handle special actions
        QString action = config.action.toLower();
        if (action == "quit") {
            QApplication::quit();
            return;
        }
        if (action == "navigate") {
            if (config.targetPage.isEmpty()) {
                qWarning() << "Navigation button has no target_page:" << config.id;
                return;
            }
            navigateToPage(config.targetPage);
            return;
        }
        if (action == "back") {
            navigateBack();
            return;
        }
        if (action == "home") {
            navigateHome();
            return;
        }

        // Launch program
        if (!config.program.isEmpty()) {
            if (config.detached) {
                QString errorMessage;
                if (!launchDetached(config.program, config.arguments,
                                    config.workingDirectory, buttonId, &errorMessage)) {
                    QMessageBox::warning(this, "Error", errorMessage);
                }
            } else {
                QProcessEnvironment env = createTouchEnvironment();
                launchApp(config.program, config.arguments, env, config.workingDirectory, buttonId);
            }
        }
    }

    // Add file monitoring setup method:
    void setupConfigWatcher()
    {
	    if (!m_currentConfigFile.isEmpty()) {
		m_configWatcher = new QFileSystemWatcher(this);
		m_configWatcher->addPath(m_currentConfigFile);
		connect(m_configWatcher, &QFileSystemWatcher::fileChanged,
			this, &TouchAppLauncher::reloadConfiguration);
		qDebug() << "File watcher setup for:" << m_currentConfigFile;
	    } else {
		qDebug() << "No config file to monitor - using defaults";
	    }
    }

    // Add configuration reload method:
    void reloadConfiguration()
    {
	    qDebug() << "Config file changed, reloading...";

	    // Reload configuration from file
	    loadConfig();

	    // Refresh the UI with new config
	    calculateScaleFactor();
	    refreshUI();

	    // Re-add file to watcher (some systems remove it after change)
	    if (m_configWatcher && !m_currentConfigFile.isEmpty()) {
		if (!m_configWatcher->files().contains(m_currentConfigFile)) {
		    m_configWatcher->addPath(m_currentConfigFile);
		    qDebug() << "Re-added file to watcher:" << m_currentConfigFile;
		}
	    }

	    qDebug() << "Configuration reload complete";
    }

    // Add UI refresh method:
    void refreshUI()
    {
	    QWidget *centralWidget = this->centralWidget();
	    if (!centralWidget) return;

	    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(centralWidget->layout());
	    if (!mainLayout) return;

	    calculateScaleFactor();
	    calculateDynamicSizes();
	    applyMainLayoutMetrics(mainLayout);
	    applyWindowBackground();
	    m_tiles.clear();

	    // Remove existing title and button layouts.
	    while (mainLayout->count() > 0) {
		QLayoutItem *item = mainLayout->takeAt(0);
		deleteLayoutRecursively(item);
	    }

	    QWidget *titleWidget = createTitleWidget();
	    if (titleWidget) {
		mainLayout->addWidget(titleWidget);
	    }

	    // Create new button layout with current page
	    QLayout *buttonLayout = createButtonLayout();
	    if (buttonLayout) {
		mainLayout->addLayout(buttonLayout);
	    }
	    if (m_config.theme.isTiles()) {
		mainLayout->addStretch(1);
	    }

	    mainLayout->invalidate();
	    mainLayout->activate();
	    centralWidget->updateGeometry();
	    centralWidget->update();
	    update();
	    animateTilesIn();

	    qDebug() << "UI refresh complete";
    }

    // Add recursive layout deletion helper:
    void deleteLayoutRecursively(QLayoutItem *item)
    {
	    if (!item) return;

	    if (QLayout *layout = item->layout()) {
		// Recursively delete child layouts
		while (QLayoutItem *child = layout->takeAt(0)) {
		    deleteLayoutRecursively(child);
		}
	    } else if (QWidget *widget = item->widget()) {
		// Delete widget
		widget->deleteLater();
	    }

	    delete item;
    }

    void handleNetworkCommand(const QString &command)
    {
        qDebug() << "Network command received:" << command;

        QStringList parts = command.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            m_networkInterface->sendResponse("ERROR: Empty command");
            return;
        }

        QString cmd = parts[0].toLower();

        if (cmd == "list-apps") {
            QString appList = listApps();
            m_networkInterface->sendResponse(appList);
        } else if (cmd == "list-page-apps") {
            QString pageId = parts.size() >= 2 ? parts[1] : m_currentPage;
            if (!pageExists(pageId)) {
                m_networkInterface->sendResponse("ERROR: page-not-found");
            } else {
                m_networkInterface->sendResponse(listPageApps(pageId));
            }
        } else if (cmd == "get-page") {
            m_networkInterface->sendResponse(m_currentPage);
        } else if (cmd == "navigate" && parts.size() >= 2) {
            if (navigateToPage(parts[1])) {
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: page-not-found");
            }
        } else if (cmd == "back") {
            navigateBack();
            m_networkInterface->sendResponse("OK");
        } else if (cmd == "home") {
            navigateHome();
            m_networkInterface->sendResponse("OK");
        } else if (cmd == "start-app" && parts.size() >= 2) {
            QString appId = parts[1];
            bool result = startApp(appId);
            if (result) {
                m_networkInterface->sendResponse("OK");
            }
            // Error response is sent by startApp() function
        } else if (cmd == "stop-app") {
            bool result = stopApp();
            if (result) {
                m_networkInterface->sendResponse("OK");
            } else {
                m_networkInterface->sendResponse("ERROR: no-app-running");
            }
        } else if (cmd == "get-running-app") {
            QString runningApp = getRunningApp();
            m_networkInterface->sendResponse(runningApp);
        }

	// NEW COMMANDS FOR RUNTIME CONTROL:
        else if (cmd == "reload-config") {
        if (!m_currentConfigFile.isEmpty()) {
            reloadConfiguration();
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: no-config-file");
        }
        } else if (cmd == "set-button-enabled" && parts.size() >= 3) {
        QString buttonId = parts[1];
        bool enabled = (parts[2].toLower() == "true");
        bool result = setButtonEnabled(buttonId, enabled);
        if (result) {
            m_networkInterface->sendResponse("OK");
        } else {
            m_networkInterface->sendResponse("ERROR: button-not-found");
        }
        } else if (cmd == "get-button-status" && parts.size() >= 2) {
        QString buttonId = parts[1];
        QString status = getButtonStatus(buttonId);
        m_networkInterface->sendResponse(status);
        } else if (cmd == "list-all-buttons") {
        QStringList allButtons;
        for (const ButtonConfig &config : m_config.buttons) {
            QString status = config.enabled ? "enabled" : "disabled";
            allButtons << QString("%1:%2").arg(config.id, status);
        }
        m_networkInterface->sendResponse(allButtons.join(","));
        } else {
            m_networkInterface->sendResponse("ERROR: Unknown command");
        }
    }

private:
    QFileSystemWatcher *m_configWatcher;
    QString m_currentConfigFile;
    LauncherConfig m_config;
    QString m_configPath;
    int m_networkPort;
    NetworkInterface *m_networkInterface;
    QProcess *m_runningProcess;
    QString m_runningAppId;
    QString m_currentPage;
    QStringList m_pageStack;
    QMap<QString, QString> m_pageTitles;
    double m_scaleFactor;  // Scale factor for responsive layout
    bool m_forceFixedLayout;  // CLI override to disable dynamic layout
    int m_dynamicButtonWidth;  // Calculated button width for dynamic layout
    int m_dynamicButtonHeight; // Calculated button height for dynamic layout
    int m_titleHeight;  // Estimated title widget height
    QList<QPointer<TileButton>> m_tiles;  // Tiles on the current page, for the entrance animation
    bool m_activationPending = false;     // A tile press is waiting out its ripple

    int tileHeaderHeight() const
    {
        QScreen *screen = QApplication::primaryScreen();
        int screenHeight = screen ? screen->size().height() : 720;
        return qBound(64, (int)(screenHeight * 0.14), 160);
    }

    void applyWindowBackground()
    {
        QString color = m_config.theme.isTiles() ? m_config.theme.backgroundColor : QString("#000000");
        setStyleSheet(QString("QMainWindow { background-color: %1; }").arg(color));
    }

    // Diagonal wave: each tile starts a little after its upper-left neighbour
    void animateTilesIn()
    {
        if (!m_config.theme.isTiles() || !m_config.theme.animations) return;
        for (const QPointer<TileButton> &tile : m_tiles) {
            if (tile) tile->playReveal(60 + (tile->gridRow() + tile->gridColumn()) * 70);
        }
    }

    void calculateScaleFactor()
    {
        // Get actual screen size
        QScreen *screen = QApplication::primaryScreen();
        if (!screen) {
            qWarning() << "Could not get primary screen, using scale factor 1.0";
            m_scaleFactor = 1.0;
            return;
        }

        QSize screenSize = screen->size();
        int screenWidth = screenSize.width();
        int screenHeight = screenSize.height();

        // Calculate scale factors based on config window size
        double scaleX = (double)screenWidth / m_config.windowWidth;
        double scaleY = (double)screenHeight / m_config.windowHeight;

        // Use the smaller scale to ensure everything fits
        m_scaleFactor = qMin(scaleX, scaleY);

        // Clamp scale factor to reasonable range (0.3 to 1.5)
        m_scaleFactor = qBound(0.3, m_scaleFactor, 1.5);

        qDebug() << "Screen size:" << screenWidth << "x" << screenHeight;
        qDebug() << "Config window size:" << m_config.windowWidth << "x" << m_config.windowHeight;
        qDebug() << "Scale factors - X:" << scaleX << "Y:" << scaleY << "Using:" << m_scaleFactor;
    }

    void calculateDynamicSizes()
    {
        // Check if dynamic layout is enabled
        bool useDynamic = m_config.layout.dynamicLayout && !m_forceFixedLayout;

        if (!useDynamic) {
            qDebug() << "Dynamic layout disabled, using fixed scaled sizes";
            m_dynamicButtonWidth = 0;
            m_dynamicButtonHeight = 0;
            return;
        }

        // Get screen size
        QScreen *screen = QApplication::primaryScreen();
        if (!screen) {
            qWarning() << "Could not get primary screen for dynamic sizing";
            return;
        }

        QSize screenSize = screen->size();
        int screenWidth = screenSize.width();
        int screenHeight = screenSize.height();

        // Calculate scaled margins
        int scaledTopMargin = (int)(m_config.layout.topMargin * m_scaleFactor);
        int scaledBottomMargin = (int)(m_config.layout.bottomMargin * m_scaleFactor);
        int scaledLeftMargin = (int)(m_config.layout.leftMargin * m_scaleFactor);
        int scaledRightMargin = (int)(m_config.layout.rightMargin * m_scaleFactor);
        int scaledSpacing = (int)(m_config.layout.spacing * m_scaleFactor);
        int scaledMainSpacing = (int)(20 * m_scaleFactor);

        // Estimate title height (font size + padding)
        int scaledTitleFontSize = qMax(16, (int)(m_config.title.fontSize * m_scaleFactor));
        int scaledTitlePadding = qMax(5, (int)(15 * m_scaleFactor));
        m_titleHeight = scaledTitleFontSize + scaledTitlePadding * 2 + scaledMainSpacing;
        if (m_config.theme.isTiles()) {
            m_titleHeight = tileHeaderHeight() + scaledMainSpacing;
        }

        // Calculate available content area
        int availableWidth = screenWidth - scaledLeftMargin - scaledRightMargin;
        int availableHeight = screenHeight - scaledTopMargin - scaledBottomMargin - m_titleHeight;

        // For grid layout, calculate button sizes to fill the grid
        if (m_config.layout.type == "grid") {
            int cols = m_config.layout.columns;
            int rows = m_config.layout.rows;

            // Account for spacing between buttons
            int totalHSpacing = (cols - 1) * scaledSpacing;
            int totalVSpacing = (rows - 1) * scaledSpacing;

            m_dynamicButtonWidth = (availableWidth - totalHSpacing) / cols;
            m_dynamicButtonHeight = (availableHeight - totalVSpacing) / rows;

            // Ensure minimum reasonable sizes
            m_dynamicButtonWidth = qMax(100, m_dynamicButtonWidth);
            m_dynamicButtonHeight = qMax(60, m_dynamicButtonHeight);

            qDebug() << "Dynamic layout enabled:";
            qDebug() << "  Available area:" << availableWidth << "x" << availableHeight;
            qDebug() << "  Grid:" << cols << "x" << rows;
            qDebug() << "  Dynamic button size:" << m_dynamicButtonWidth << "x" << m_dynamicButtonHeight;
        } else {
            // For vertical/horizontal layouts, use available width and distribute height
            m_dynamicButtonWidth = availableWidth;

            // Count enabled buttons
            int enabledCount = buttonsForPage(m_currentPage).count();

            if (enabledCount > 0) {
                int totalVSpacing = (enabledCount - 1) * scaledSpacing;
                m_dynamicButtonHeight = (availableHeight - totalVSpacing) / enabledCount;
                m_dynamicButtonHeight = qMax(60, m_dynamicButtonHeight);
            }

            qDebug() << "Dynamic layout (vertical/horizontal):";
            qDebug() << "  Dynamic button size:" << m_dynamicButtonWidth << "x" << m_dynamicButtonHeight;
        }
    }

    void setupNetworkInterface()
    {
        m_networkInterface = new NetworkInterface(m_networkPort, this);
        connect(m_networkInterface, &NetworkInterface::commandReceived,
                this, &TouchAppLauncher::handleNetworkCommand);

        if (m_networkInterface->startServer()) {
            qDebug() << "Network interface started on port" << m_networkPort;
        } else {
            qWarning() << "Failed to start network interface on port" << m_networkPort;
        }
    }

    void applyMainLayoutMetrics(QVBoxLayout *mainLayout)
    {
        if (!mainLayout) return;

        int scaledTopMargin = (int)(m_config.layout.topMargin * m_scaleFactor);
        int scaledBottomMargin = (int)(m_config.layout.bottomMargin * m_scaleFactor);
        int scaledLeftMargin = (int)(m_config.layout.leftMargin * m_scaleFactor);
        int scaledRightMargin = (int)(m_config.layout.rightMargin * m_scaleFactor);

        mainLayout->setSpacing((int)(20 * m_scaleFactor));
        mainLayout->setContentsMargins(scaledLeftMargin, scaledTopMargin,
                                      scaledRightMargin, scaledBottomMargin);
    }

    QString normalizePageId(const QString &pageId) const
    {
        return pageId.isEmpty() ? QString("home") : pageId;
    }

    bool findButton(const QString &buttonId, ButtonConfig &button, bool enabledOnly = false) const
    {
        for (const ButtonConfig &config : m_config.buttons) {
            if (config.id == buttonId && (!enabledOnly || config.enabled)) {
                button = config;
                return true;
            }
        }
        return false;
    }

    QList<ButtonConfig> buttonsForPage(const QString &pageId, bool enabledOnly = true) const
    {
        QList<ButtonConfig> buttons;
        QString normalizedPage = normalizePageId(pageId);

        for (const ButtonConfig &config : m_config.buttons) {
            if (config.page == normalizedPage && config.visible && (!enabledOnly || config.enabled)) {
                buttons.append(config);
            }
        }

        return buttons;
    }

    bool pageExists(const QString &pageId) const
    {
        QString normalizedPage = normalizePageId(pageId);
        if (normalizedPage == "home") {
            return true;
        }

        if (m_pageTitles.contains(normalizedPage)) {
            return true;
        }

        for (const ButtonConfig &config : m_config.buttons) {
            if (config.page == normalizedPage || config.targetPage == normalizedPage) {
                return true;
            }
        }

        return false;
    }

    QString currentTitleText() const
    {
        if (m_currentPage == "home") {
            return m_config.title.text;
        }

        return m_pageTitles.value(m_currentPage, m_currentPage);
    }

    bool navigateToPage(const QString &pageId, bool pushHistory = true)
    {
        QString normalizedPage = normalizePageId(pageId);
        if (!pageExists(normalizedPage)) {
            qWarning() << "Page not found:" << normalizedPage;
            return false;
        }

        if (normalizedPage == m_currentPage) {
            return true;
        }

        if (pushHistory) {
            m_pageStack.append(m_currentPage);
        }

        m_currentPage = normalizedPage;
        refreshUI();
        return true;
    }

    void navigateBack()
    {
        if (!m_pageStack.isEmpty()) {
            m_currentPage = m_pageStack.takeLast();
        } else {
            m_currentPage = "home";
        }

        refreshUI();
    }

    void navigateHome()
    {
        m_pageStack.clear();
        m_currentPage = "home";
        refreshUI();
    }

    QString listApps()
    {
        QStringList appIds;
        for (const ButtonConfig &config : m_config.buttons) {
            if (config.enabled && config.visible) {
                appIds << config.id;
            }
        }
        return appIds.join(",");
    }

    QString listPageApps(const QString &pageId)
    {
        QStringList appIds;
        for (const ButtonConfig &config : buttonsForPage(pageId)) {
            appIds << config.id;
        }
        return appIds.join(",");
    }

    bool startApp(const QString &appId)
    {
        // Check if an app is already running
        if (m_runningProcess && m_runningProcess->state() == QProcess::Running) {
            m_networkInterface->sendResponse("ERROR: app-already-running");
            return false;
        }

        // Find the app configuration across all pages for backward-compatible automation.
        ButtonConfig config;
        if (!findButton(appId, config, true)) {
            m_networkInterface->sendResponse("ERROR: invalid-app-name");
            return false;
        }

        // Handle special actions
        QString action = config.action.toLower();
        if (action == "quit") {
            m_networkInterface->sendResponse("OK");
            QApplication::quit();
            return true;
        }
        if (action == "navigate") {
            if (config.targetPage.isEmpty() || !navigateToPage(config.targetPage)) {
                m_networkInterface->sendResponse("ERROR: page-not-found");
                return false;
            }
            return true;
        }
        if (action == "back") {
            navigateBack();
            return true;
        }
        if (action == "home") {
            navigateHome();
            return true;
        }

        // Validate program exists before launching
        if (config.program.isEmpty()) {
            m_networkInterface->sendResponse("ERROR: app-has-no-program");
            return false;
        }

        if (!QFile::exists(config.program)) {
            m_networkInterface->sendResponse("ERROR: program-not-found");
            return false;
        }

        if (config.detached) {
            QString errorMessage;
            if (!launchDetached(config.program, config.arguments,
                                config.workingDirectory, appId, &errorMessage)) {
                m_networkInterface->sendResponse(QString("ERROR: %1").arg(errorMessage));
                return false;
            }

            m_networkInterface->sendResponse("OK");
            return true;
        }

        // Launch the app asynchronously
        m_networkInterface->sendResponse("OK");
        launchAppAsync(config.program, config.arguments, config.workingDirectory, appId);
        return true;
    }

    QString getRunningApp()
    {
        if (m_runningProcess && m_runningProcess->state() == QProcess::Running) {
            return m_runningAppId;
        }
        return "none";
    }

    bool stopApp()
    {
        // Check if an app is currently running
        if (!m_runningProcess || m_runningProcess->state() != QProcess::Running) {
            return false; // No app running
        }

        qDebug() << "Stopping app:" << m_runningAppId;

        // First try graceful termination (SIGTERM)
        m_runningProcess->terminate();

        // Wait up to 3 seconds for graceful shutdown
        if (m_runningProcess->waitForFinished(3000)) {
            qDebug() << "App terminated gracefully:" << m_runningAppId;
        } else {
            // If app doesn't respond to SIGTERM, force kill it
            qWarning() << "App didn't respond to SIGTERM, force killing:" << m_runningAppId;
            m_runningProcess->kill();

            // Wait another 2 seconds for force kill to complete
            if (!m_runningProcess->waitForFinished(2000)) {
                qWarning() << "Failed to force kill app:" << m_runningAppId;
                // Process cleanup will happen via finished() signal eventually
            } else {
                qDebug() << "App force killed:" << m_runningAppId;
            }
        }

        // Note: Process cleanup and state reset happens automatically
        // via the finished() signal handler we already connected in launchAppAsync()

        return true;
    }

    void loadConfig()
    {
        // Determine config file path with priority:
        // 1. Command line argument
        // 2. /etc/launcher.json (default)
        // 3. ./launcher.json (current directory fallback)
        // 4. Built-in defaults

        QString configFile = m_configPath;

        if (configFile.isEmpty()) {
            // Try default locations
            if (QFile::exists("/etc/launcher.json")) {
                configFile = "/etc/launcher.json";
            } else if (QFile::exists("./launcher.json")) {
                configFile = "./launcher.json";
                qDebug() << "Using local launcher.json from current directory";
            }
        }

        if (configFile.isEmpty() || !QFile::exists(configFile)) {
            qWarning() << "No JSON config found, using default configuration";
            qWarning() << "Searched paths:";
            qWarning() << "  - Command line argument:" << (m_configPath.isEmpty() ? "none" : m_configPath);
            qWarning() << "  - /etc/launcher.json";
            qWarning() << "  - ./launcher.json";
            m_currentConfigFile = "";
            loadDefaultConfig();
            return;
        }

        qDebug() << "Loading configuration from:" << configFile;
        m_currentConfigFile = configFile;

        QFile file(configFile);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Could not open config file:" << configFile;
            loadDefaultConfig();
            return;
        }

        QByteArray data = file.readAll();
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);

        if (error.error != QJsonParseError::NoError) {
            qWarning() << "JSON parse error in" << configFile << ":" << error.errorString();
            qWarning() << "Falling back to default configuration";
            loadDefaultConfig();
            return;
        }

        parseJsonConfig(doc.object());
        qDebug() << "Successfully loaded configuration from:" << configFile;
    }

    void parseJsonConfig(const QJsonObject &root)
    {
        m_config = LauncherConfig();
        m_pageTitles.clear();

        QJsonObject launcher = root["launcher"].toObject();

        // Parse window settings
        QJsonObject window = launcher["window"].toObject();
        m_config.windowWidth = window["width"].toInt(800);
        m_config.windowHeight = window["height"].toInt(600);

        // Parse title configuration
        QJsonObject title = launcher["title"].toObject();
        m_config.title.text = title["text"].toString("Touch Applications");
        m_config.title.logoPath = title["logo"].toString();

        QJsonObject logoSize = title["logo_size"].toObject();
        m_config.title.logoWidth = logoSize["width"].toInt(150);
        m_config.title.logoHeight = logoSize["height"].toInt(60);

        m_config.title.layout = title["layout"].toString("text_only");
        m_config.title.fontSize = title["font_size"].toInt(32);
        m_config.title.color = title["color"].toString("#ffffff");
        m_config.title.subtitle = title["subtitle"].toString();

        // Parse theme (optional; absent means the classic look)
        QJsonObject theme = launcher["theme"].toObject();
        ThemeConfig &t = m_config.theme;
        t.style = theme["style"].toString(t.style).toLower();
        t.fontFamily = theme["font_family"].toString(t.fontFamily);
        t.backgroundColor = theme["background_color"].toString(t.backgroundColor);
        t.gridColor = theme["grid_color"].toString(t.gridColor);
        t.gridSpacing = theme["grid_spacing"].toInt(t.gridSpacing);
        t.cornerMarks = theme["corner_marks"].toBool(t.cornerMarks);
        t.colorBars = theme["color_bars"].toBool(t.colorBars);
        t.cardColor = theme["card_color"].toString(t.cardColor);
        t.cardHoverColor = theme["card_hover_color"].toString(t.cardHoverColor);
        t.cardBorderColor = theme["card_border_color"].toString(t.cardBorderColor);
        t.textColor = theme["text_color"].toString(t.textColor);
        t.subtextColor = theme["subtext_color"].toString(t.subtextColor);
        t.showClock = theme["show_clock"].toBool(t.showClock);
        t.showIp = theme["show_ip"].toBool(t.showIp);
        t.showResolution = theme["show_resolution"].toBool(t.showResolution);
        t.animations = theme["animations"].toBool(t.animations);

        // Parse layout configuration
        QJsonObject layout = launcher["layout"].toObject();
        m_config.layout.type = layout["type"].toString("vertical");
        m_config.layout.columns = layout["columns"].toInt(2);
        m_config.layout.rows = layout["rows"].toInt(2);
        m_config.layout.spacing = layout["spacing"].toInt(15);

        QJsonObject margins = layout["margins"].toObject();
        m_config.layout.topMargin = margins["top"].toInt(50);
        m_config.layout.bottomMargin = margins["bottom"].toInt(50);
        m_config.layout.leftMargin = margins["left"].toInt(50);
        m_config.layout.rightMargin = margins["right"].toInt(50);

        // Parse dynamic layout option (default: true for smart sizing)
        m_config.layout.dynamicLayout = layout["dynamic_layout"].toBool(true);

        // Parse buttons
        QJsonArray buttons = launcher["buttons"].toArray();
        for (const QJsonValue &value : buttons) {
            QJsonObject btnObj = value.toObject();
            ButtonConfig btn;

            btn.id = btnObj["id"].toString();
            btn.enabled = btnObj["enabled"].toBool(true);
            btn.visible = btnObj["visible"].toBool(true);
            btn.text = btnObj["text"].toString();
            btn.iconPath = btnObj["icon"].toString();
            btn.program = btnObj["program"].toString();
            btn.workingDirectory = btnObj["working_directory"].toString("/tmp");
            btn.action = btnObj["action"].toString();
            btn.page = normalizePageId(btnObj["page"].toString("home"));
            btn.targetPage = btnObj["target_page"].toString();
            btn.pageTitle = btnObj["page_title"].toString();
            btn.detached = btnObj["detached"].toBool(false)
                || btnObj["launch_mode"].toString().toLower() == "detached";

            // Parse arguments array
            QJsonArray args = btnObj["arguments"].toArray();
            for (const QJsonValue &arg : args) {
                btn.arguments << arg.toString();
            }

            // Parse position
            QJsonObject pos = btnObj["position"].toObject();
            btn.row = pos["row"].toInt(0);
            btn.column = pos["column"].toInt(0);
            btn.columnSpan = pos["column_span"].toInt(1);
            btn.rowSpan = pos["row_span"].toInt(1);

            // Parse size
            QJsonObject size = btnObj["size"].toObject();
            btn.width = size["width"].toInt(200);
            btn.height = size["height"].toInt(100);

            // Parse icon settings
            QJsonObject iconSize = btnObj["icon_size"].toObject();
            btn.iconWidth = iconSize["width"].toInt(48);
            btn.iconHeight = iconSize["height"].toInt(48);
            btn.iconLayout = btnObj["icon_layout"].toString("icon_left");

            // Parse styling
            btn.fontSize = btnObj["font_size"].toInt(24);
            btn.backgroundColor = btnObj["background_color"].toString("#404040");
            btn.hoverColor = btnObj["hover_color"].toString("#505050");
            btn.subtitle = btnObj["subtitle"].toString();
            btn.accentColor = btnObj["accent_color"].toString();
            btn.borderRadius = btnObj["border_radius"].toInt(15);

            if (btn.action.toLower() == "navigate") {
                if (btn.targetPage.isEmpty()) {
                    btn.targetPage = btn.id;
                }
                btn.targetPage = normalizePageId(btn.targetPage);
                QString pageTitle = btn.pageTitle.isEmpty() ? btn.text : btn.pageTitle;
                m_pageTitles.insert(btn.targetPage, pageTitle);
            }

            // Add all buttons (including disabled ones) to config for network API
            m_config.buttons.append(btn);
        }

        QStringList validStack;
        for (const QString &pageId : m_pageStack) {
            if (pageExists(pageId)) {
                validStack.append(pageId);
            }
        }
        m_pageStack = validStack;

        if (!pageExists(m_currentPage)) {
            m_currentPage = "home";
            m_pageStack.clear();
        }
    }

    void loadDefaultConfig()
    {
        // Fallback to hardcoded configuration
        m_config = LauncherConfig();
        m_pageTitles.clear();
        m_pageStack.clear();
        m_currentPage = "home";

        ButtonConfig fingerPaint;
        fingerPaint.id = "fingerpaint";
        fingerPaint.text = "🎨 Finger Paint";
        fingerPaint.program = "/usr/lib/qt/examples/widgets/touch/fingerpaint/fingerpaint";
        fingerPaint.row = 0; fingerPaint.column = 0;

        ButtonConfig scribble;
        scribble.id = "scribble";
        scribble.text = "✏️ Scribble";
        scribble.program = "/usr/lib/qt/examples/widgets/widgets/scribble/scribble";
        scribble.row = 1; scribble.column = 0;

        ButtonConfig gallery;
        gallery.id = "gallery";
        gallery.text = "📷 Photo Gallery";
        gallery.program = "/usr/bin/touch-gallery";
        gallery.arguments << "/Pictures";
        gallery.workingDirectory = "/Pictures";
        gallery.row = 2; gallery.column = 0;

        ButtonConfig slideshow;
        slideshow.id = "slideshow";
        slideshow.text = "🎞️ Slideshow";
        slideshow.program = "/usr/bin/touch-gallery";
        slideshow.arguments << "/Pictures" << "slideshow" << "5";
        slideshow.workingDirectory = "/Pictures";
        slideshow.backgroundColor = "#2e8b57";
        slideshow.hoverColor = "#3cb371";
        slideshow.row = 3; slideshow.column = 0;

        ButtonConfig exit;
        exit.id = "exit";
        exit.text = "❌ Exit";
        exit.action = "quit";
        exit.backgroundColor = "#8b0000";
        exit.hoverColor = "#a00000";
        exit.height = 80;
        exit.row = 4; exit.column = 0;

        m_config.buttons << fingerPaint << scribble << gallery << slideshow << exit;
    }

    QProcessEnvironment createTouchEnvironment()
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

        // Child apps inherit QT_QPA_PLATFORM from parent (systemEnvironment)
        // No need to explicitly set it - this allows both linuxfb and eglfs to work

        // Only set touch device if not already set by system environment
        // This allows init scripts or systemd to configure the correct device
        if (!env.contains("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS")) {
            env.insert("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS", "/dev/input/event0");
        }

        // Set font directory if not already set
        if (!env.contains("QT_QPA_FONTDIR")) {
            env.insert("QT_QPA_FONTDIR", "/usr/share/fonts/dejavu/");
        }

        // Set runtime directory if not already set
        if (!env.contains("XDG_RUNTIME_DIR")) {
            env.insert("XDG_RUNTIME_DIR", "/tmp/runtime-root");
        }

        return env;
    }

    void launchAppAsync(const QString &program, const QStringList &args,
                       const QString &workingDir, const QString &appId)
    {
        // Create runtime directory if needed
        QDir().mkpath("/tmp/runtime-root");

        // Clean up any previous process
        if (m_runningProcess) {
            m_runningProcess->deleteLater();
            m_runningProcess = nullptr;
        }

        // Create process environment
        QProcessEnvironment env = createTouchEnvironment();

        m_runningProcess = new QProcess(this);
        m_runningProcess->setProcessEnvironment(env);
        m_runningProcess->setWorkingDirectory(workingDir);

        // Signals can arrive from a process that a newer launch has already
        // superseded (deleteLater() defers destruction and ~QProcess kills a
        // still-running child), so each handler must verify it belongs to the
        // current process before touching shared state.
        QProcess *proc = m_runningProcess;

        // Connect to process signals for async handling
        connect(proc, &QProcess::started,
                [this, proc, program, appId]() {
                    if (m_runningProcess != proc)
                        return;
                    m_runningAppId = appId;
                    qDebug() << "App started successfully:" << program << "App ID:" << appId;
                    // Hide launcher after successful start
                    this->hide();
                });

        connect(proc, &QProcess::errorOccurred,
                [this, proc, program, appId](QProcess::ProcessError error) {
                    qWarning() << "Failed to start app:" << program << "App ID:" << appId << "Error:" << error;
                    if (m_runningProcess != proc)
                        return;
                    // Only FailedToStart ends without a finished() signal;
                    // Crashed etc. are cleaned up by the finished() handler.
                    if (error != QProcess::FailedToStart)
                        return;
                    // Don't send network response here - connection already closed
                    m_runningAppId = "";
                    m_runningProcess = nullptr;
                    proc->deleteLater();
                });

        // Show launcher again when app exits
        connect(proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this, proc, program, appId](int exitCode, QProcess::ExitStatus /*exitStatus*/) {
                    qDebug() << "App finished:" << program << "App ID:" << appId << "Exit code:" << exitCode;
                    if (m_runningProcess != proc)
                        return;
                    this->show();
                    m_runningAppId = "";
                    m_runningProcess = nullptr;
                    proc->deleteLater();
                });

        // Start the process (non-blocking)
        proc->start(program, args);

        qDebug() << "Process start initiated for:" << program << "with args:" << args << "App ID:" << appId;
    }

    bool launchApp(const QString &program, const QStringList &args,
                   const QProcessEnvironment &env, const QString &workingDir, const QString &appId)
    {
        // This is the original synchronous version for button clicks
        // Check if program exists
        if (!QFile::exists(program)) {
            QMessageBox::warning(this, "Error",
                QString("Program not found: %1").arg(program));
            return false;
        }

        // Create runtime directory if needed
        QDir().mkpath("/tmp/runtime-root");

        // Clean up any previous process
        if (m_runningProcess) {
            m_runningProcess->deleteLater();
            m_runningProcess = nullptr;
        }

        m_runningProcess = new QProcess(this);
        m_runningProcess->setProcessEnvironment(env);
        m_runningProcess->setWorkingDirectory(workingDir);

        // Launch the program
        m_runningProcess->start(program, args);

        if (!m_runningProcess->waitForStarted(3000)) {
            QString errorMsg = QString("Failed to start: %1\nError: %2").arg(program, m_runningProcess->errorString());
            QMessageBox::warning(this, "Error", errorMsg);
            m_runningProcess->deleteLater();
            m_runningProcess = nullptr;
            return false;
        }

        m_runningAppId = appId;
        qDebug() << "Launched:" << program << "with args:" << args << "App ID:" << appId;

        // Hide launcher while app is running
        this->hide();

        // Show launcher again when app exits; guard against signals from a
        // process that a newer launch has already superseded.
        QProcess *proc = m_runningProcess;
        connect(proc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [this, proc, program, appId](int exitCode, QProcess::ExitStatus /*exitStatus*/) {
                    qDebug() << "App finished:" << program << "App ID:" << appId << "Exit code:" << exitCode;
                    if (m_runningProcess != proc)
                        return;
                    this->show();
                    m_runningAppId = "";
                    m_runningProcess = nullptr;
                    proc->deleteLater();
                });

        return true;
    }

    bool launchDetached(const QString &program, const QStringList &args,
                        const QString &workingDir, const QString &appId,
                        QString *errorMessage = nullptr)
    {
        if (!QFile::exists(program)) {
            if (errorMessage) {
                *errorMessage = QString("Program not found: %1").arg(program);
            }
            return false;
        }

        qint64 pid = 0;
        if (!QProcess::startDetached(program, args, workingDir, &pid)) {
            if (errorMessage) {
                *errorMessage = QString("Failed to start detached app: %1").arg(program);
            }
            return false;
        }

        qDebug() << "Detached app started:" << program << "with args:" << args
                 << "App ID:" << appId << "PID:" << pid;
        return true;
    }

    void setupUI()
    {
        setWindowTitle("Touch App Launcher");
        applyWindowBackground();

        QWidget *centralWidget = new BackdropWidget(&m_config.theme);
        setCentralWidget(centralWidget);

        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        applyMainLayoutMetrics(mainLayout);

        // Create title section
        QWidget *titleWidget = createTitleWidget();
        if (titleWidget) {
            mainLayout->addWidget(titleWidget);
        }

        // Create buttons layout - only for enabled buttons in UI
        QLayout *buttonLayout = createButtonLayout();
        if (buttonLayout) {
            mainLayout->addLayout(buttonLayout);
        }
        if (m_config.theme.isTiles()) {
            mainLayout->addStretch(1);
        }

        // Don't set minimum size - let it adapt to screen
        // setMinimumSize(m_config.windowWidth, m_config.windowHeight);
    }

    QWidget* createTitleWidget()
    {
        QString titleText = currentTitleText();

        if (m_config.title.layout == "text_only" && titleText.isEmpty()) {
            return nullptr; // No title
        }

        if (m_config.theme.isTiles()) {
            // Sub-pages show where we are, e.g. "Home > Calibration Tools"
            QString subtitle = m_config.title.subtitle;
            if (m_currentPage != "home") {
                QStringList crumbs;
                for (const QString &pageId : m_pageStack) {
                    crumbs << (pageId == "home" ? QString("Home") : m_pageTitles.value(pageId, pageId));
                }
                crumbs << titleText;
                subtitle = crumbs.join(QString::fromUtf8("  \u203A  "));
            }
            QString logo = m_config.title.layout == "text_only" ? QString() : m_config.title.logoPath;
            return new LauncherHeader(m_config.theme, titleText, subtitle, logo,
                                      m_networkPort, tileHeaderHeight());
        }

        QWidget *titleWidget = new QWidget;
        QHBoxLayout *titleLayout = new QHBoxLayout(titleWidget);
        titleLayout->setContentsMargins(0, 0, 0, 0);

        // Create logo if specified - apply scaling to logo size
        QLabel *logoLabel = nullptr;
        if (!m_config.title.logoPath.isEmpty() && QFile::exists(m_config.title.logoPath)
            && m_config.title.layout != "text_only") {
            logoLabel = new QLabel;
            QPixmap logo(m_config.title.logoPath);
            int scaledLogoWidth = (int)(m_config.title.logoWidth * m_scaleFactor);
            int scaledLogoHeight = (int)(m_config.title.logoHeight * m_scaleFactor);
            logo = logo.scaled(scaledLogoWidth, scaledLogoHeight,
                              Qt::KeepAspectRatio, Qt::SmoothTransformation);
            logoLabel->setPixmap(logo);
            logoLabel->setAlignment(Qt::AlignCenter);
        }

        // Create text label if specified - apply scaling to font size and padding
        QLabel *textLabel = nullptr;
        if (!titleText.isEmpty() && m_config.title.layout != "logo_only") {
            textLabel = new QLabel(titleText);
            textLabel->setAlignment(Qt::AlignCenter);

            // Title font should remain readable - use higher minimum (28px) and less aggressive scaling
            int scaledTitleFontSize = qMax(28, (int)(m_config.title.fontSize * qMax(0.8, m_scaleFactor)));
            int scaledTitlePadding = qMax(10, (int)(15 * m_scaleFactor));
            QString style = QString("QLabel { color: %1; font-size: %2px; font-weight: bold; padding: %3px; }")
                           .arg(m_config.title.color).arg(scaledTitleFontSize).arg(scaledTitlePadding);
            textLabel->setStyleSheet(style);
        }

        // Arrange logo and text based on layout
        if (m_config.title.layout == "logo_left" && logoLabel && textLabel) {
            titleLayout->addWidget(logoLabel);
            titleLayout->addWidget(textLabel);
        } else if (m_config.title.layout == "logo_right" && logoLabel && textLabel) {
            titleLayout->addWidget(textLabel);
            titleLayout->addWidget(logoLabel);
        } else if (m_config.title.layout == "logo_only" && logoLabel) {
            titleLayout->addWidget(logoLabel);
        } else if (textLabel) {
            titleLayout->addWidget(textLabel);
        }

        if (m_config.title.layout == "logo_top" && logoLabel && textLabel) {
            delete titleLayout; // Replace with vertical layout
            QVBoxLayout *vLayout = new QVBoxLayout(titleWidget);
            vLayout->setContentsMargins(0, 0, 0, 0);
            vLayout->addWidget(logoLabel);
            vLayout->addWidget(textLabel);
        }

        return titleWidget;
    }

    QLayout* createButtonLayout()
    {
        QList<ButtonConfig> enabledButtons = buttonsForPage(m_currentPage);

        if (enabledButtons.isEmpty()) {
            return nullptr;
        }

        if (m_config.layout.type == "grid") {
            return createGridLayout(enabledButtons);
        } else if (m_config.layout.type == "horizontal") {
            return createHorizontalLayout(enabledButtons);
        } else {
            return createVerticalLayout(enabledButtons);
        }
    }

    QGridLayout* createGridLayout(const QList<ButtonConfig> &buttons)
    {
        QGridLayout *gridLayout = new QGridLayout;
        gridLayout->setSpacing((int)(m_config.layout.spacing * m_scaleFactor));

        for (const ButtonConfig &config : buttons) {
            QPushButton *button = createButton(config);
            gridLayout->addWidget(button, config.row, config.column,
                                 config.rowSpan, config.columnSpan);
        }

        return gridLayout;
    }

    QVBoxLayout* createVerticalLayout(const QList<ButtonConfig> &buttons)
    {
        QVBoxLayout *vLayout = new QVBoxLayout;
        vLayout->setSpacing((int)(m_config.layout.spacing * m_scaleFactor));

        for (const ButtonConfig &config : buttons) {
            QPushButton *button = createButton(config);
            vLayout->addWidget(button);
        }

        return vLayout;
    }

    QHBoxLayout* createHorizontalLayout(const QList<ButtonConfig> &buttons)
    {
        QHBoxLayout *hLayout = new QHBoxLayout;
        hLayout->setSpacing((int)(m_config.layout.spacing * m_scaleFactor));

        for (const ButtonConfig &config : buttons) {
            QPushButton *button = createButton(config);
            hLayout->addWidget(button);
        }

        return hLayout;
    }

    QPushButton* createButton(const ButtonConfig &config)
    {
        // Determine button size: use dynamic if enabled, otherwise use scaled fixed size
        int buttonWidth, buttonHeight;
        bool useDynamic = m_config.layout.dynamicLayout && !m_forceFixedLayout
                          && m_dynamicButtonWidth > 0 && m_dynamicButtonHeight > 0;

        if (useDynamic) {
            buttonWidth = m_dynamicButtonWidth;
            buttonHeight = m_dynamicButtonHeight;
        } else {
            // Fall back to scaled fixed size from config
            buttonWidth = (int)(config.width * m_scaleFactor);
            buttonHeight = (int)(config.height * m_scaleFactor);
        }

        if (m_config.theme.isTiles()) {
            bool missing = !config.program.isEmpty() && !QFile::exists(config.program);
            TileButton *tile = new TileButton(config, m_config.theme, missing);
            tile->setProperty("buttonId", config.id);
            tile->setFixedSize(buttonWidth, buttonHeight);
            // Launch on press (as the classic buttons do), but let the ripple
            // play briefly before the launcher hides or the page changes
            QString buttonId = config.id;
            int delay = m_config.theme.animations ? 160 : 0;
            connect(tile, &QPushButton::pressed, this, [this, buttonId, delay]() {
                if (m_activationPending) return;
                m_activationPending = true;
                QTimer::singleShot(delay, this, [this, buttonId]() {
                    m_activationPending = false;
                    activateButton(buttonId);
                });
            });
            m_tiles.append(tile);
            return tile;
        }

        QPushButton *button = new QPushButton;
        button->setProperty("buttonId", config.id);
        button->setFocusPolicy(Qt::NoFocus);

        button->setMinimumSize(buttonWidth, buttonHeight);
        button->setMaximumSize(buttonWidth, buttonHeight);

        // Calculate icon scale factor based on button size ratio
        double iconScaleFactor;
        if (useDynamic) {
            // Scale icons proportionally to button size change
            double widthRatio = (double)buttonWidth / config.width;
            double heightRatio = (double)buttonHeight / config.height;
            iconScaleFactor = qMin(widthRatio, heightRatio);
        } else {
            iconScaleFactor = m_scaleFactor;
        }

        int scaledIconWidth = (int)(config.iconWidth * iconScaleFactor);
        int scaledIconHeight = (int)(config.iconHeight * iconScaleFactor);

        // Set icon if available
        if (!config.iconPath.isEmpty() && QFile::exists(config.iconPath)
            && config.iconLayout != "text_only") {
            QPixmap iconPixmap(config.iconPath);
            QPixmap scaledIcon = iconPixmap.scaled(scaledIconWidth, scaledIconHeight,
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
            button->setIcon(QIcon(scaledIcon));
            button->setIconSize(QSize(scaledIconWidth, scaledIconHeight));
        }

        // Set text based on layout
        if (config.iconLayout != "icon_only") {
            button->setText(config.text);
        }

        // Apply scaling to font size, border radius, padding, margin
        // Use iconScaleFactor for consistent scaling in dynamic mode
        int scaledFontSize = qMax(12, (int)(config.fontSize * iconScaleFactor));  // Minimum 12px
        int scaledBorderRadius = (int)(config.borderRadius * iconScaleFactor);
        int scaledPadding = qMax(5, (int)(10 * iconScaleFactor));
        int scaledMargin = qMax(2, (int)(5 * iconScaleFactor));
        int scaledBorder = qMax(1, (int)(3 * iconScaleFactor));

        // Set style with scaled values
        QString buttonStyle = QString(
            "QPushButton { "
            "  background-color: %1; "
            "  color: white; "
            "  border: %5px solid #606060; "
            "  border-radius: %2px; "
            "  padding: %6px; "
            "  font-size: %3px; "
            "  font-weight: bold; "
            "  margin: %7px; "
            "}"
            "QPushButton:hover { "
            "  background-color: %4; "
            "  border-color: #808080; "
            "}"
            "QPushButton:pressed { "
            "  background-color: #303030; "
            "  border-color: #404040; "
            "}")
            .arg(config.backgroundColor)
            .arg(scaledBorderRadius)
            .arg(scaledFontSize)
            .arg(config.hoverColor)
            .arg(scaledBorder)
            .arg(scaledPadding)
            .arg(scaledMargin);

        button->setStyleSheet(buttonStyle);

        // Use pressed signal instead of clicked for reliable touch response
        // clicked fires on release which can miss short touches on touchscreens
        connect(button, &QPushButton::pressed, this, &TouchAppLauncher::buttonClicked);

        return button;
    }

    void validatePrograms()
    {
        for (const ButtonConfig &config : m_config.buttons) {
            if (!config.program.isEmpty() && !QFile::exists(config.program)) {
                qWarning() << "Program not found:" << config.program << "for button:" << config.id;
            } else if (!config.program.isEmpty()) {
                qDebug() << "Found program:" << config.program << "for button:" << config.id;
            }
        }

        // Check if Pictures directory exists, create if not
        if (!QDir("/Pictures").exists()) {
            QDir().mkpath("/Pictures");
            qDebug() << "Created /Pictures directory";
        }
    }

    // Add button enable/disable method:
    bool setButtonEnabled(const QString &buttonId, bool enabled)
    {
	for (ButtonConfig &btn : m_config.buttons) {
		if (btn.id == buttonId) {
			if (btn.enabled != enabled) {
				btn.enabled = enabled;
				qDebug() << "Button" << buttonId << (enabled ? "enabled" : "disabled");
				refreshUI();
			}
			return true;
		}
	}
	qWarning() << "Button not found:" << buttonId;
	return false;
    }
    // Add button status query method:
    QString getButtonStatus(const QString &buttonId)
    {
	for (const ButtonConfig &btn : m_config.buttons) {
		if (btn.id == buttonId) {
			return btn.enabled ? "enabled" : "disabled";
		}
	}
	return "ERROR: button-not-found";
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("Touch App Launcher");
    app.setApplicationVersion("2.0");

    // Parse command line arguments
    QString configPath;
    int networkPort = DEFAULT_LAUNCHER_PORT; // Default port
    bool showHelp = false;
    bool forceFixedLayout = false;

    for (int i = 1; i < argc; i++) {
        QString arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            showHelp = true;
            break;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                configPath = argv[i + 1];
                i++; // Skip next argument
            } else {
                qWarning() << "Error: --config requires a file path";
                return 1;
            }
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                bool ok;
                networkPort = QString(argv[i + 1]).toInt(&ok);
                if (!ok || networkPort < 1 || networkPort > 65535) {
                    qWarning() << "Error: --port requires a valid port number (1-65535)";
                    return 1;
                }
                i++; // Skip next argument
            } else {
                qWarning() << "Error: --port requires a port number";
                return 1;
            }
        } else if (arg == "--fixed-layout") {
            forceFixedLayout = true;
        } else if (!arg.startsWith("-")) {
            // Assume it's a config file path if no option specified
            configPath = arg;
        } else {
            qWarning() << "Unknown option:" << arg;
            showHelp = true;
            break;
        }
    }

    if (showHelp) {
        qDebug() << "Touch App Launcher v2.0";
        qDebug() << "";
        qDebug() << "Usage:" << argv[0] << "[OPTIONS] [CONFIG_FILE]";
        qDebug() << "";
        qDebug() << "Options:";
        qDebug() << "  -c, --config FILE    Use specified JSON configuration file";
        qDebug() << "  -p, --port PORT      Network interface port (default:" << DEFAULT_LAUNCHER_PORT << ")";
        qDebug() << "  --fixed-layout       Disable dynamic layout (use scaled fixed sizes from config)";
        qDebug() << "  -h, --help           Show this help message";
        qDebug() << "";
        qDebug() << "Examples:";
        qDebug() << " " << argv[0] << "                              # Use default config and port" << DEFAULT_LAUNCHER_PORT;
        qDebug() << " " << argv[0] << "--port 8090                   # Use port 8090";
        qDebug() << " " << argv[0] << "/tmp/my-launcher.json          # Use specific config file";
        qDebug() << " " << argv[0] << "--config /tmp/my-launcher.json --port 8090 # Use specific config and port";
        qDebug() << "";
        qDebug() << "Config file search order:";
        qDebug() << "  1. Command line argument";
        qDebug() << "  2. /etc/launcher.json";
        qDebug() << "  3. ./launcher.json (current directory)";
        qDebug() << "  4. Built-in defaults";
        qDebug() << "";
        qDebug() << "Network API Commands:";
        qDebug() << "  list-apps                    # List all enabled buttons across all pages";
        qDebug() << "  list-page-apps [page-id]     # List visible buttons on current or specified page";
        qDebug() << "  get-page                     # Get current launcher page";
        qDebug() << "  navigate <page-id>           # Navigate to page";
        qDebug() << "  back                         # Navigate to previous page";
        qDebug() << "  home                         # Navigate to home page";
        qDebug() << "  start-app <app-id>          # Start specific application";
        qDebug() << "  stop-app                    # Stop currently running application";
        qDebug() << "  get-running-app             # Get currently running app or 'none'";
	qDebug() << "  reload-config               # Reload JSON configuration from file";
	qDebug() << "  set-button-enabled <id> <true|false>  # Enable/disable button";
	qDebug() << "  get-button-status <id>      # Get button status (enabled/disabled)";
	qDebug() << "  list-all-buttons            # List all buttons with their status";
	return 0;
    }

    // Create and show launcher
    TouchAppLauncher launcher(configPath, networkPort, forceFixedLayout);
    launcher.showMaximized(); // Full screen for embedded device

    return app.exec();
}

#include "main.moc"
