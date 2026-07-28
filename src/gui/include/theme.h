#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

class QApplication;

// 应用主题：Fusion + 自定义调色板 + QSS，跨平台外观一致，跟随系统深浅色。
// 所有颜色常量集中在此，其余代码通过 Theme::color() 或 QPalette 取值。
class Theme : public QObject
{
    Q_OBJECT

public:
    enum class Role {
        Accent,
        AccentHover,
        AccentPressed,
        Background,      // 窗口底色
        Surface,         // 卡片/输入控件底色
        SurfaceHover,
        Border,
        BorderStrong,
        TextPrimary,
        TextSecondary,
        TextDisabled,
        Success,
        Warning,
        Danger,
        Info,
    };

    // 间距（4pt 网格）
    static constexpr int SpacingXs = 4;
    static constexpr int SpacingSm = 8;
    static constexpr int SpacingMd = 12;
    static constexpr int SpacingLg = 16;
    static constexpr int SpacingXl = 24;

    // 控件高度
    static constexpr int ControlHeight = 28;
    static constexpr int ControlHeightLarge = 32;

    // 字号
    static constexpr int FontCaption = 11;
    static constexpr int FontBody = 13;
    static constexpr int FontTitle = 15;
    static constexpr int FontDisplay = 20;

    static Theme* instance();

    // 安装 Fusion style、调色板与 QSS，并订阅系统深浅色变化。
    static void apply(QApplication& app);

    static QColor color(Role role);
    static bool isDark();

    // 把带 alpha 的单色剪影图标重绘为指定颜色，浅深色模式下均可见。
    static QIcon tintedIcon(const QString& resourcePath, Role role = Role::TextPrimary);

signals:
    // 系统深浅色切换后发出，供持有自绘资源（图标、缓存画刷）的界面刷新。
    void themeChanged();

private:
    explicit Theme(QObject* parent = nullptr);

    void reload();

    QApplication* m_app = nullptr;
};
