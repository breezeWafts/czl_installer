#include "Theme.h"

namespace {

/// 把颜色 + 圆角 + 内边距展开成一个按钮样式块。
QString buttonStyle(const char *base, const char *hover, const char *pressed)
{
    return QStringLiteral(
               "QPushButton {"
               "   background:%1; color:#ffffff;"
               "   border:none; border-radius:%4px;"
               "   padding:6px 18px; font-weight:bold;"
               "}"
               "QPushButton:hover   { background:%2; }"
               "QPushButton:pressed { background:%3; }"
               "QPushButton:disabled {"
               "   background:#cbd5e0; color:#f7fafc;"
               "}")
        .arg(QLatin1String(base),
             QLatin1String(hover),
             QLatin1String(pressed))
        .arg(Theme::kRadius);
}

} // namespace

namespace Theme {

QString globalStyleSheet()
{
    return QStringLiteral(
               // ---------------- 全局 ----------------
               "QWidget {"
               "   color:%1;"
               "   font-family:'Microsoft YaHei UI','Microsoft YaHei','Segoe UI',sans-serif;"
               "   font-size:9.5pt;"
               "}"
               "QMainWindow, QDialog { background:%2; }"

               // ---------------- 输入类 ----------------
               "QComboBox {"
               "   background:%3; border:1px solid %4; border-radius:%6px;"
               "   padding:5px 10px; min-height:20px;"
               "}"
               "QComboBox:hover { border-color:%5; }"
               "QComboBox:focus { border-color:%5; }"
               "QComboBox::drop-down {"
               "   subcontrol-origin:padding; subcontrol-position:center right;"
               "   width:22px; border:none;"
               "}"
               "QComboBox QAbstractItemView {"
               "   background:%3; border:1px solid %4; border-radius:4px;"
               "   padding:4px; outline:none;"
               "   selection-background-color:%5; selection-color:#ffffff;"
               "}"

               // ---------------- 树 ----------------
               "QTreeView {"
               "   background:%3; border:none; outline:none;"
               "   padding:6px 4px;"
               "   show-decoration-selected:1;"
               "}"
               "QTreeView::item {"
               "   padding:6px 8px; border-radius:5px; margin:1px 4px;"
               "}"
               "QTreeView::item:hover    { background:#e8f1fb; }"
               "QTreeView::item:selected {"
               "   background:#d3e4f8; color:%7; font-weight:bold;"
               "}"
               "QTreeView::branch { background:transparent; }"

               // ---------------- 列表 ----------------
               "QListWidget {"
               "   background:%8; border:none; outline:none; padding:8px;"
               "}"
               "QListWidget::item:selected { background:transparent; }"

               // ---------------- 进度条 ----------------
               "QProgressBar {"
               "   background:#e2e8f0; border:none; border-radius:2px;"
               "}"
               "QProgressBar::chunk { background:%5; border-radius:2px; }"

               // ---------------- 滚动条（细窄、无边框，贴近现代风格）----------------
               "QScrollBar:vertical {"
               "   background:transparent; width:10px; margin:2px;"
               "}"
               "QScrollBar::handle:vertical {"
               "   background:#c3ccd8; border-radius:4px; min-height:32px;"
               "}"
               "QScrollBar::handle:vertical:hover { background:#a8b4c4; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
               "   height:0; background:transparent;"
               "}"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
               "   background:transparent;"
               "}"
               "QScrollBar:horizontal {"
               "   background:transparent; height:10px; margin:2px;"
               "}"
               "QScrollBar::handle:horizontal {"
               "   background:#c3ccd8; border-radius:4px; min-width:32px;"
               "}"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
               "   width:0; background:transparent;"
               "}"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
               "   background:transparent;"
               "}"

               // ---------------- 状态栏 ----------------
               "QStatusBar {"
               "   background:%3; border-top:1px solid %4;"
               "}"
               "QStatusBar::item { border:none; }"

               // ---------------- 提示气泡 ----------------
               "QToolTip {"
               "   background:#2d3748; color:#f7fafc;"
               "   border:1px solid #1a202c; border-radius:6px;"
               "   padding:8px 10px;"
               "}"

               // ---------------- 消息框 ----------------
               "QMessageBox { background:%3; }"
               "QMessageBox QLabel { color:%1; }"
               "QMessageBox QPushButton {"
               "   background:%5; color:#ffffff; border:none;"
               "   border-radius:6px; padding:6px 20px; min-width:76px;"
               "   font-weight:bold;"
               "}"
               "QMessageBox QPushButton:hover   { background:%9; }"
               "QMessageBox QPushButton:pressed { background:%7; }"

               // ---------------- 分割条 ----------------
               "QSplitter::handle { background:%4; }"
               "QSplitter::handle:horizontal { width:1px; }")
        .arg(QLatin1String(kTextPrimary))      // %1
        .arg(QLatin1String(kCanvas))           // %2
        .arg(QLatin1String(kSurface))          // %3
        .arg(QLatin1String(kBorder))           // %4
        .arg(QLatin1String(kPrimary))          // %5
        .arg(kRadius)                          // %6
        .arg(QLatin1String(kPrimaryDark))      // %7
        .arg(QLatin1String(kSurfaceAlt))       // %8
        .arg(QLatin1String(kPrimaryHover));    // %9
}

QString installButtonStyle()
{
    return buttonStyle(kSuccess, kSuccessHover, "#276749");
}

QString retryButtonStyle()
{
    return buttonStyle(kWarning, kWarningHover, "#9c4221");
}

QString primaryButtonStyle()
{
    return buttonStyle(kPrimary, kPrimaryHover, kPrimaryPressed);
}

QString statusColor(bool success)
{
    return QLatin1String(success ? kSuccess : kDanger);
}

QString neutralStatusColor()
{
    return QLatin1String(kTextSecondary);
}

} // namespace Theme
