#include "../include/theme.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QStyleHints>

namespace {

struct Swatch {
    QRgb light;
    QRgb dark;
};

// 唯一的颜色定义处。新增颜色只应加在这里。
const QHash<Theme::Role, Swatch>& swatches()
{
    static const QHash<Theme::Role, Swatch> table = {
        { Theme::Role::Accent,        { 0xFF2F6FED, 0xFF4C8DFF } },
        { Theme::Role::AccentHover,   { 0xFF255FD4, 0xFF6BA1FF } },
        { Theme::Role::AccentPressed, { 0xFF1C4FB4, 0xFF3D7BE0 } },
        { Theme::Role::Background,    { 0xFFF4F5F7, 0xFF1C1D20 } },
        { Theme::Role::Surface,       { 0xFFFFFFFF, 0xFF26282C } },
        { Theme::Role::SurfaceHover,  { 0xFFECEEF1, 0xFF32343A } },
        { Theme::Role::Border,        { 0xFFDDDFE4, 0xFF3A3D43 } },
        { Theme::Role::BorderStrong,  { 0xFFC2C6CE, 0xFF4E525A } },
        { Theme::Role::TextPrimary,   { 0xFF1B1D21, 0xFFE9EAEE } },
        { Theme::Role::TextSecondary, { 0xFF6B6F78, 0xFF9EA2AC } },
        { Theme::Role::TextDisabled,  { 0xFFA8ACB4, 0xFF63666E } },
        { Theme::Role::Success,       { 0xFF1F9254, 0xFF48C07C } },
        { Theme::Role::Warning,       { 0xFFB4700E, 0xFFE0A138 } },
        { Theme::Role::Danger,        { 0xFFC33A33, 0xFFF07A72 } },
        { Theme::Role::Info,          { 0xFF2F6FED, 0xFF4C8DFF } },
    };
    return table;
}

QPalette buildPalette()
{
    const QColor accent = Theme::color(Theme::Role::Accent);
    const QColor background = Theme::color(Theme::Role::Background);
    const QColor surface = Theme::color(Theme::Role::Surface);
    const QColor textPrimary = Theme::color(Theme::Role::TextPrimary);
    const QColor textDisabled = Theme::color(Theme::Role::TextDisabled);

    QPalette palette;
    palette.setColor(QPalette::Window, background);
    palette.setColor(QPalette::WindowText, textPrimary);
    palette.setColor(QPalette::Base, surface);
    palette.setColor(QPalette::AlternateBase, background);
    palette.setColor(QPalette::Text, textPrimary);
    palette.setColor(QPalette::Button, surface);
    palette.setColor(QPalette::ButtonText, textPrimary);
    palette.setColor(QPalette::BrightText, Theme::color(Theme::Role::Danger));
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, QColor(Qt::white));
    palette.setColor(QPalette::ToolTipBase, surface);
    palette.setColor(QPalette::ToolTipText, textPrimary);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::LinkVisited, accent);
    palette.setColor(QPalette::PlaceholderText, Theme::color(Theme::Role::TextSecondary));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, Theme::color(Theme::Role::Border));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, textDisabled);
    return palette;
}

QString hex(Theme::Role role)
{
    return Theme::color(role).name(QColor::HexRgb);
}

QString expandTokens(QString qss)
{
    const QHash<QString, QString> tokens = {
        { QStringLiteral("%ACCENT%"),         hex(Theme::Role::Accent) },
        { QStringLiteral("%ACCENT_HOVER%"),   hex(Theme::Role::AccentHover) },
        { QStringLiteral("%ACCENT_PRESSED%"), hex(Theme::Role::AccentPressed) },
        { QStringLiteral("%BACKGROUND%"),     hex(Theme::Role::Background) },
        { QStringLiteral("%SURFACE%"),        hex(Theme::Role::Surface) },
        { QStringLiteral("%SURFACE_HOVER%"),  hex(Theme::Role::SurfaceHover) },
        { QStringLiteral("%BORDER%"),         hex(Theme::Role::Border) },
        { QStringLiteral("%BORDER_STRONG%"),  hex(Theme::Role::BorderStrong) },
        { QStringLiteral("%TEXT_PRIMARY%"),   hex(Theme::Role::TextPrimary) },
        { QStringLiteral("%TEXT_SECONDARY%"), hex(Theme::Role::TextSecondary) },
        { QStringLiteral("%TEXT_DISABLED%"),  hex(Theme::Role::TextDisabled) },
        { QStringLiteral("%SUCCESS%"),        hex(Theme::Role::Success) },
        { QStringLiteral("%WARNING%"),        hex(Theme::Role::Warning) },
        { QStringLiteral("%DANGER%"),         hex(Theme::Role::Danger) },
        { QStringLiteral("%SPACING_XS%"),     QString::number(Theme::SpacingXs) },
        { QStringLiteral("%SPACING_SM%"),     QString::number(Theme::SpacingSm) },
        { QStringLiteral("%SPACING_MD%"),     QString::number(Theme::SpacingMd) },
        { QStringLiteral("%SPACING_LG%"),     QString::number(Theme::SpacingLg) },
        { QStringLiteral("%SPACING_XL%"),     QString::number(Theme::SpacingXl) },
        { QStringLiteral("%CONTROL_HEIGHT%"), QString::number(Theme::ControlHeight) },
        { QStringLiteral("%CONTROL_HEIGHT_LARGE%"), QString::number(Theme::ControlHeightLarge) },
        { QStringLiteral("%FONT_CAPTION%"),   QString::number(Theme::FontCaption) },
        { QStringLiteral("%FONT_BODY%"),      QString::number(Theme::FontBody) },
        { QStringLiteral("%FONT_TITLE%"),     QString::number(Theme::FontTitle) },
        { QStringLiteral("%FONT_DISPLAY%"),   QString::number(Theme::FontDisplay) },
        // 深浅色各自的箭头图标，QSS 的 url() 无法运行时着色。
        { QStringLiteral(":/chevron-down.png"),
          Theme::isDark() ? QStringLiteral(":/chevron-down-dark.png")
                          : QStringLiteral(":/chevron-down-light.png") },
        { QStringLiteral(":/chevron-up.png"),
          Theme::isDark() ? QStringLiteral(":/chevron-up-dark.png")
                          : QStringLiteral(":/chevron-up-light.png") },
    };

    for (auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
        qss.replace(it.key(), it.value());
    }
    return qss;
}

} // namespace

Theme::Theme(QObject* parent)
    : QObject(parent)
{
}

Theme* Theme::instance()
{
    static Theme theme;
    return &theme;
}

bool Theme::isDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QColor Theme::color(Role role)
{
    const auto it = swatches().constFind(role);
    Q_ASSERT_X(it != swatches().constEnd(), "Theme::color", "role has no swatch");
    return QColor::fromRgba(isDark() ? it->dark : it->light);
}

QIcon Theme::tintedIcon(const QString& resourcePath, Role role)
{
    QPixmap source(resourcePath);
    if (source.isNull()) {
        qWarning("Theme::tintedIcon: 无法加载图标资源 %s", qPrintable(resourcePath));
        return QIcon();
    }

    QPixmap tinted(source.size());
    tinted.setDevicePixelRatio(source.devicePixelRatio());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color(role));
    painter.end();

    return QIcon(tinted);
}

void Theme::apply(QApplication& app)
{
    Theme* theme = instance();
    theme->m_app = &app;

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    theme->reload();

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
        theme, [theme]() { theme->reload(); });
}

void Theme::reload()
{
    if (!m_app) {
        return;
    }

    m_app->setPalette(buildPalette());

    QFile file(QStringLiteral(":/app.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Theme::reload: 无法读取 :/app.qss，界面将回退到未套用样式的状态");
        return;
    }

    const QString qss = expandTokens(QString::fromUtf8(file.readAll()));
    if (qss.contains(QLatin1Char('%'))) {
        qWarning("Theme::reload: app.qss 仍有未替换的占位符，样式可能不完整");
    }

    m_app->setStyleSheet(qss);
    emit themeChanged();
}
