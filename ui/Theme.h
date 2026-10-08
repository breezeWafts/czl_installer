#ifndef THEME_H
#define THEME_H

#include <QString>

/**
 * @brief 全局视觉样式（配色 + Qt 样式表）。
 *
 * 把配色与样式集中到一处，取代旧版散落在各个控件上的
 * setStyleSheet("...color:#666...") 硬编码字符串 —— 那样既难统一，
 * 又容易在不同控件上出现近似但不一致的灰色。
 *
 * 设计要点：
 *   - 主色沿用原来的蓝色系（#2b6cb0），但补全 hover / pressed / disabled 三态；
 *   - 中性色收敛为几级（正文 / 次要 / 弱化 / 边框 / 背景）；
 *   - 给出统一的行高、圆角、内边距常量，让卡片列表看起来整齐。
 */
namespace Theme {

// ---------------------------------------------------------------------------
// 配色
// ---------------------------------------------------------------------------
inline constexpr const char *kPrimary        = "#2b6cb0";  // 主色
inline constexpr const char *kPrimaryHover   = "#3a82d2";
inline constexpr const char *kPrimaryPressed = "#1f4f85";
inline constexpr const char *kPrimaryDark    = "#1a4a80";  // 深色主色（选中文字）

inline constexpr const char *kSuccess        = "#2f855a";  // 安装
inline constexpr const char *kSuccessHover   = "#38a169";
inline constexpr const char *kWarning        = "#c05621";  // 重试
inline constexpr const char *kWarningHover   = "#dd6b20";
inline constexpr const char *kDanger         = "#c53030";  // 失败

inline constexpr const char *kTextPrimary    = "#1a202c";  // 正文
inline constexpr const char *kTextSecondary  = "#4a5568";  // 次要
inline constexpr const char *kTextMuted      = "#718096";  // 弱化
inline constexpr const char *kBorder         = "#d9dee5";
inline constexpr const char *kBorderStrong   = "#c3cad4";
inline constexpr const char *kSurface        = "#ffffff";
inline constexpr const char *kSurfaceAlt     = "#f7f9fc";
inline constexpr const char *kCanvas         = "#eef2f7";  // 窗口底色
inline constexpr const char *kHeaderBg       = "#eaf0f7";  // 面板标题条

// ---------------------------------------------------------------------------
// 尺寸
// ---------------------------------------------------------------------------
inline constexpr int kCardHeight   = 104;  // 列表条目高度
inline constexpr int kRadius       = 8;    // 圆角
inline constexpr int kButtonWidth  = 104;
inline constexpr int kButtonHeight = 36;

/// 整个应用的全局样式表（在 main() 里 setStyleSheet 一次即可）。
QString globalStyleSheet();

/// 安装按钮：可安装状态。
QString installButtonStyle();
/// 安装按钮：失败后的「重试」状态。
QString retryButtonStyle();
/// 主色按钮（用于状态栏的「设置浏览器书签」这类次要动作）。
QString primaryButtonStyle();

/// 状态栏文字颜色：根据成功/失败/普通三种语义取色。
QString statusColor(bool success);
/// 普通/进行中状态文字颜色。
QString neutralStatusColor();

} // namespace Theme

#endif // THEME_H
