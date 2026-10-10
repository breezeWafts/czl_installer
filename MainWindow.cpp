#include "MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "BookmarkManager.h"
#include "ConfigManager.h"
#include "DownloadManager.h"
#include "InstallManager.h"
#include "InstallItemModel.h"
#include "Logger.h"
#include "Theme.h"
#include "TreeModel.h"

namespace {

/// 卡片类型的中文标签 + 配色，让右侧列表一眼能区分安装方式。
struct TypeBadge {
    const char *label;
    const char *bg;
    const char *fg;
};

/// 卡片类型徽标。
/// 注意：label 是普通 C 字符串（UTF-8 字节），取出时必须用
/// QString::fromUtf8 —— 用 fromLatin1 会显示成 "å¤å¶" 这样的乱码。
TypeBadge badgeForType(const QString &type)
{
    if (type == QLatin1String("msi"))  return {"MSI",  "#ebf4ff", "#2b6cb0"};
    if (type == QLatin1String("exe"))  return {"EXE",  "#e6fffa", "#2c7a7b"};
    if (type == QLatin1String("bat"))  return {"BAT",  "#fffaf0", "#9c4221"};
    if (type == QLatin1String("copy_only"))
        return {"复制", "#f0fff4", "#2f855a"};
    if (type == QLatin1String("copy_and_register"))
        return {"复制+注册", "#faf5ff", "#6b46c1"};
    return {"未知", "#edf2f7", "#4a5568"};
}

QString statusTextFor(const QString &status)
{
    if (status == QLatin1String(InstallItemModel::kStatusInstalling))
        return QStringLiteral("安装中...");
    if (status == QLatin1String(InstallItemModel::kStatusDone))
        return QStringLiteral("已安装");
    if (status == QLatin1String(InstallItemModel::kStatusFailed))
        return QStringLiteral("重试");
    return QStringLiteral("安装");
}

} // namespace

// ===========================================================================
// 构造 / 析构
// ===========================================================================
MainWindow::MainWindow(ConfigManager *config,
                       DownloadManager *downloadManager,
                       BookmarkManager *bookmarkManager,
                       InstallManager *installManager,
                       TreeModel *treeModel,
                       InstallItemModel *itemModel,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_config(config)
    , m_dl(downloadManager)
    , m_bm(bookmarkManager)
    , m_im(installManager)
    , m_treeModel(treeModel)
    , m_itemModel(itemModel)
{
    setWindowTitle(QStringLiteral("czl_installer · 软件安装管理器"));
    resize(1240, 800);
    setMinimumSize(940, 600);

    applyWindowIcon();
    setupUi();
    setupConnections();

    // 启动时下拉框必须反映真实的配置来源（启动参数可能就是 --pg）
    {
        const QSignalBlocker blocker(m_serverTypeCombo);
        m_serverTypeCombo->setCurrentIndex(
            m_config->currentSource() == ConfigManager::ConfigSource::PostgreSQL
                ? 1
                : 0);
    }

    // 初始状态：载入类别树并选中第一项，列表不为空
    m_treeModel->reload();
    m_treeView->expandAll();

    const QModelIndex first = m_treeModel->firstLeafOrTopIndex();
    if (first.isValid())
        m_treeView->setCurrentIndex(first);
    else {
        m_itemModel->loadAll();
        rebuildItemList();
    }

    onActiveServerChanged();
    onBusyChanged(false);
}

MainWindow::~MainWindow() = default;

// ===========================================================================
// 图标
// ===========================================================================
void MainWindow::applyWindowIcon()
{
    // 可执行文件图标由 app.rc 的 RT_GROUP_ICON 提供；
    // 这里再设置一次窗口/任务栏图标，并显式覆盖任务栏分组图标。
    QIcon icon(QStringLiteral(":/icons/app.png"));
    if (icon.isNull())
        icon = QIcon(QStringLiteral("app.ico"));

    if (!icon.isNull()) {
        setWindowIcon(icon);
        QApplication::setWindowIcon(icon);
    }
}

// ===========================================================================
// UI
// ===========================================================================
QWidget *MainWindow::createPanelHeader(const QString &title, const QString &subtitle)
{
    auto *header = new QWidget;
    header->setObjectName(QStringLiteral("panelHeader"));
    header->setStyleSheet(QStringLiteral(
        "QWidget#panelHeader {"
        "   background:%1;"
        "   border-bottom:1px solid %2;"
        "}").arg(QLatin1String(Theme::kHeaderBg),
                 QLatin1String(Theme::kBorder)));

    auto *layout = new QVBoxLayout(header);
    layout->setContentsMargins(14, 9, 14, 9);
    layout->setSpacing(1);

    auto *titleLabel = new QLabel(title, header);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    titleLabel->setFont(titleFont);

    auto *subtitleLabel = new QLabel(subtitle, header);
    subtitleLabel->setStyleSheet(
        QStringLiteral("color:%1; font-size:8.5pt;")
            .arg(QLatin1String(Theme::kTextMuted)));

    layout->addWidget(titleLabel);
    if (!subtitle.isEmpty())
        layout->addWidget(subtitleLabel);
    else
        subtitleLabel->hide();

    return header;
}

void MainWindow::setupUi()
{
    // ---------------- 顶部工具栏 ----------------
    auto *toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("topBar"));
    toolbar->setStyleSheet(QStringLiteral(
        "QWidget#topBar {"
        "   background:%1;"
        "   border-bottom:1px solid %2;"
        "}").arg(QLatin1String(Theme::kSurface),
                 QLatin1String(Theme::kBorder)));

    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(16, 10, 16, 10);
    toolbarLayout->setSpacing(10);

    // 左侧：品牌名
    auto *brandLabel = new QLabel(QStringLiteral("czl_installer"), toolbar);
    QFont brandFont = brandLabel->font();
    brandFont.setBold(true);
    brandFont.setPointSize(brandFont.pointSize() + 2);
    brandLabel->setFont(brandFont);
    brandLabel->setStyleSheet(
        QStringLiteral("color:%1;").arg(QLatin1String(Theme::kPrimary)));
    toolbarLayout->addWidget(brandLabel);

    // 分隔符
    auto *separator = new QFrame(toolbar);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet(
        QStringLiteral("color:%1;").arg(QLatin1String(Theme::kBorder)));
    separator->setFixedHeight(22);
    toolbarLayout->addWidget(separator);

    // 服务器类型
    auto *serverLabel = new QLabel(QStringLiteral("服务器"), toolbar);
    serverLabel->setStyleSheet(
        QStringLiteral("color:%1;").arg(QLatin1String(Theme::kTextSecondary)));

    m_serverTypeCombo = new QComboBox(toolbar);
    m_serverTypeCombo->addItem(QStringLiteral("JSON · FTP 存储"));
    m_serverTypeCombo->addItem(QStringLiteral("PostgreSQL · HTTP 存储"));
    m_serverTypeCombo->setMinimumWidth(210);
    m_serverTypeCombo->setToolTip(
        QStringLiteral("模式一：config/config_ftp.json + FTP 软件包目录\n"
                       "模式二：PostgreSQL 数据库（czl_* 表）+ HTTP 网络服务器路径"));

    m_reloadConfigBtn = new QPushButton(QStringLiteral("刷新配置"), toolbar);
    m_reloadConfigBtn->setMinimumHeight(26);
    m_reloadConfigBtn->setToolTip(
        QStringLiteral("按当前来源重新加载配置（Web 管理端改完后点这里）"));
    m_reloadConfigBtn->setStyleSheet(Theme::primaryButtonStyle());

    toolbarLayout->addWidget(serverLabel);
    toolbarLayout->addWidget(m_serverTypeCombo);
    toolbarLayout->addWidget(m_reloadConfigBtn);
    toolbarLayout->addStretch(1);

    // 右侧：当前服务器状态胶囊
    m_activeServerLabel = new QLabel(toolbar);
    m_activeServerLabel->setAlignment(Qt::AlignCenter);
    m_activeServerLabel->setMinimumHeight(26);
    m_activeServerLabel->setContentsMargins(12, 0, 12, 0);
    toolbarLayout->addWidget(m_activeServerLabel);

    // ---------------- 主体：左树右列表 ----------------
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    // ===== 左侧：类别树 =====
    auto *leftPanel = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    leftLayout->addWidget(
        createPanelHeader(QStringLiteral("软件类别"),
                          QStringLiteral("选择类别以筛选右侧列表")));

    m_treeView = new QTreeView(leftPanel);
    m_treeView->setModel(m_treeModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setAnimated(true);
    m_treeView->setIndentation(16);
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    leftLayout->addWidget(m_treeView, 1);
    leftPanel->setMinimumWidth(210);
    leftPanel->setMaximumWidth(420);

    // ===== 右侧：可安装程序列表 =====
    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    rightLayout->addWidget(
        createPanelHeader(QStringLiteral("可安装程序"),
                          QStringLiteral("点击卡片右侧按钮执行安装")));

    m_listWidget = new QListWidget(rightPanel);
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setSpacing(8);
    m_listWidget->setFocusPolicy(Qt::NoFocus);

    rightLayout->addWidget(m_listWidget, 1);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 960});

    // ---------------- 整体布局 ----------------
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    // ---------------- 底部状态栏 ----------------
    m_statusLabel = new QLabel(QStringLiteral("就绪"), this);
    m_statusLabel->setStyleSheet(
        QStringLiteral("color:%1; padding-left:10px;")
            .arg(QLatin1String(Theme::kTextSecondary)));
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->setSizeGripEnabled(true);

    m_localDirLabel = new QLabel(
        QStringLiteral("缓存目录 %1").arg(m_config->localBaseDir()), this);
    m_localDirLabel->setStyleSheet(
        QStringLiteral("color:%1; padding-right:10px;")
            .arg(QLatin1String(Theme::kTextMuted)));
    m_localDirLabel->setToolTip(m_config->localBaseDir());
    statusBar()->addPermanentWidget(m_localDirLabel);

    m_setBookmarksBtn = new QPushButton(QStringLiteral("设置浏览器书签"), this);
    m_setBookmarksBtn->setMinimumHeight(24);
    m_setBookmarksBtn->setToolTip(
        QStringLiteral("为所有已安装的 Chromium 系浏览器写入配置中的书签"));
    m_setBookmarksBtn->setStyleSheet(Theme::primaryButtonStyle());
    statusBar()->addPermanentWidget(m_setBookmarksBtn);
    statusBar()->setContentsMargins(0, 2, 8, 2);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(6000);
}

// ===========================================================================
// 信号连接
// ===========================================================================
void MainWindow::setupConnections()
{
    connect(m_serverTypeCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onServerTypeChanged);

    connect(m_reloadConfigBtn, &QPushButton::clicked,
            this, &MainWindow::onReloadConfigClicked);

    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onTreeCurrentChanged);

    connect(m_setBookmarksBtn, &QPushButton::clicked,
            this, &MainWindow::onSetBookmarksClicked);

    connect(m_statusTimer, &QTimer::timeout,
            this, &MainWindow::onResetStatusText);

    connect(m_im, &InstallManager::installProgress,
            this, &MainWindow::onInstallProgress);
    connect(m_im, &InstallManager::installFinished,
            this, &MainWindow::onInstallFinished);
    connect(m_im, &InstallManager::activeServerChanged,
            this, &MainWindow::onActiveServerChanged);
    connect(m_im, &InstallManager::sourceChanged,
            this, &MainWindow::onSourceChanged);
    connect(m_im, &InstallManager::busyChanged,
            this, &MainWindow::onBusyChanged);
}

// ===========================================================================
// 槽函数
// ===========================================================================
void MainWindow::onServerTypeChanged(int index)
{
    if (index < 0)
        return;

    // 索引 -> 配置来源。模式一（JSON + FTP）/ 模式二（PG + HTTP）。
    const ConfigManager::ConfigSource source =
        (index == 1) ? ConfigManager::ConfigSource::PostgreSQL
                     : ConfigManager::ConfigSource::JsonFile;

    if (source == m_config->currentSource()) {
        // 只是界面上重复选中，不触发加载
        return;
    }

    if (!m_im->setConfigSource(source)) {
        // 切换失败：把下拉框拨回真实来源，避免界面与数据不一致
        const QSignalBlocker blocker(m_serverTypeCombo);
        m_serverTypeCombo->setCurrentIndex(
            m_config->currentSource() == ConfigManager::ConfigSource::PostgreSQL ? 1 : 0);
        return;
    }

    refreshAfterConfigReload();
}

void MainWindow::onReloadConfigClicked()
{
    if (!m_im->reloadConfig())
        return;

    refreshAfterConfigReload();
}

void MainWindow::onSourceChanged(const QString &description, bool ok,
                                 const QString &message)
{
    Q_UNUSED(description)

    if (!message.isEmpty())
        setStatus(message, Theme::statusColor(ok));
    if (message.isEmpty())
        return;

    m_statusTimer->start();

    // 同步下拉框（切换可能由 InstallManager 内部触发）
    const QSignalBlocker blocker(m_serverTypeCombo);
    m_serverTypeCombo->setCurrentIndex(
        m_config->currentSource() == ConfigManager::ConfigSource::PostgreSQL ? 1 : 0);
}

void MainWindow::refreshAfterConfigReload()
{
    m_treeModel->reload();
    m_treeView->expandAll();

    const QModelIndex first = m_treeModel->firstLeafOrTopIndex();
    if (first.isValid())
        m_treeView->setCurrentIndex(first);
    else {
        m_itemModel->loadAll();
        rebuildItemList();
    }

    m_localDirLabel->setText(
        QStringLiteral("缓存目录 %1").arg(m_config->localBaseDir()));
    m_localDirLabel->setToolTip(m_config->localBaseDir());

    onActiveServerChanged();
}

void MainWindow::onTreeCurrentChanged(const QModelIndex &current,
                                      const QModelIndex &previous)
{
    Q_UNUSED(previous)

    if (!current.isValid())
        return;

    const QString categoryId =
        m_treeModel->data(current, TreeModel::IdRole).toString();
    if (categoryId.isEmpty())
        return;

    m_currentCategoryId = categoryId;
    Logger::info(QStringLiteral("选择类别: %1").arg(categoryId));

    m_itemModel->filterByCategory(categoryId);
    rebuildItemList();
}

void MainWindow::onInstallButtonClicked(const QString &itemId)
{
    Logger::info(QStringLiteral("用户点击安装: %1").arg(itemId));

    m_itemModel->setStatus(itemId, QString::fromLatin1(
                                      InstallItemModel::kStatusInstalling));
    refreshItemRow(itemId);

    m_im->installItem(itemId);
}

void MainWindow::onInstallProgress(const QString &itemId, int percent,
                                   const QString &stage)
{
    setStatus(stage, Theme::neutralStatusColor());

    auto it = m_rowWidgets.find(itemId);
    if (it == m_rowWidgets.end())
        return;

    if (it->stageLabel != nullptr)
        it->stageLabel->setText(stage);
    if (it->progress != nullptr && percent > 0)
        it->progress->setValue(percent);
}

void MainWindow::onInstallFinished(const QString &itemId, bool success,
                                  const QString &message)
{
    if (success)
        Logger::info(QStringLiteral("安装成功 [%1]: %2").arg(itemId, message));
    else
        Logger::error(QStringLiteral("安装失败 [%1]: %2").arg(itemId, message));

    setStatus(message, Theme::statusColor(success));

    m_itemModel->setStatus(itemId, QString::fromLatin1(
                                      success ? InstallItemModel::kStatusDone
                                              : InstallItemModel::kStatusFailed));
    refreshItemRow(itemId);
    m_statusTimer->start();
}

void MainWindow::onBusyChanged(bool busy)
{
    m_serverTypeCombo->setEnabled(!busy);
    m_reloadConfigBtn->setEnabled(!busy);
    m_setBookmarksBtn->setEnabled(!busy);

    for (auto it = m_rowWidgets.begin(); it != m_rowWidgets.end(); ++it) {
        if (it->button == nullptr)
            continue;

        const QString status = m_itemModel->statusOf(it.key());
        const bool installing =
            status == QLatin1String(InstallItemModel::kStatusInstalling);

        it->button->setEnabled(!busy && !installing);
    }
}

void MainWindow::onActiveServerChanged()
{
    const QString name = m_dl->activeServerName();
    const bool connected = !name.isEmpty();

    // 状态胶囊：已连接为绿色，未连接为灰色
    m_activeServerLabel->setText(
        connected ? QStringLiteral("●  %1").arg(name)
                  : QStringLiteral("○  未连接"));
    m_activeServerLabel->setToolTip(
        connected ? QStringLiteral("当前使用的下载服务器")
                  : QStringLiteral("首次安装时会自动探测可用服务器"));

    m_activeServerLabel->setStyleSheet(
        QStringLiteral(
            "QLabel {"
            "   background:%1; color:%2;"
            "   border:1px solid %3; border-radius:13px;"
            "   font-weight:bold; font-size:8.5pt;"
            "}")
            .arg(connected ? QStringLiteral("#e6fffa") : QStringLiteral("#f0f2f5"),
                 connected ? QLatin1String(Theme::kSuccess)
                           : QLatin1String(Theme::kTextMuted),
                 connected ? QStringLiteral("#9ae6b4")
                           : QLatin1String(Theme::kBorder)));
}

void MainWindow::onResetStatusText()
{
    setStatus(QStringLiteral("就绪"), Theme::neutralStatusColor());
}

void MainWindow::onSetBookmarksClicked()
{
    if (m_bm == nullptr)
        return;

    const QList<BookmarkItem> items = m_config->bookmarks();
    if (items.isEmpty()) {
        Logger::warn(QStringLiteral("设置浏览器书签：配置文件中没有书签项"));
        QMessageBox::information(this, QStringLiteral("设置浏览器书签"),
                                 QStringLiteral("配置文件中没有书签项"
                                                "（bookmarks 数组为空）。"));
        return;
    }

    Logger::info(QStringLiteral("用户点击设置浏览器书签，共 %1 条").arg(items.size()));

    const auto answer = QMessageBox::question(
        this, QStringLiteral("设置浏览器书签"),
        QStringLiteral("即将为所有已安装的浏览器设置 %1 条书签。\n\n"
                       "执行期间相关浏览器会被强制关闭，是否继续？")
            .arg(items.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (answer != QMessageBox::Yes) {
        Logger::info(QStringLiteral("用户取消设置浏览器书签"));
        return;
    }

    m_setBookmarksBtn->setEnabled(false);
    setStatus(QStringLiteral("正在设置浏览器书签..."), Theme::neutralStatusColor());

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const BookmarkManager::Result result = m_bm->applyToAll(items);
    QApplication::restoreOverrideCursor();

    Logger::info(QStringLiteral("设置浏览器书签完成: %1").arg(result.message));

    QString detail;
    if (!result.browsers.isEmpty())
        detail += QStringLiteral("成功: %1\n")
                      .arg(result.browsers.join(QStringLiteral(", ")));
    if (!result.failed.isEmpty())
        detail += QStringLiteral("失败: %1\n")
                      .arg(result.failed.join(QStringLiteral(", ")));
    detail += QStringLiteral("\n%1").arg(result.message);

    QMessageBox::information(this,
                             result.ok ? QStringLiteral("设置完成")
                                       : QStringLiteral("设置结果"),
                             detail);

    m_setBookmarksBtn->setEnabled(!m_im->isBusy());
    setStatus(result.message, Theme::statusColor(result.ok));
    m_statusTimer->start();
}

// ===========================================================================
// 列表渲染
// ===========================================================================
void MainWindow::setStatus(const QString &text, const QString &color)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(
        QStringLiteral("color:%1; padding-left:10px;")
            .arg(color.isEmpty() ? Theme::neutralStatusColor() : color));
}

void MainWindow::rebuildItemList()
{
    m_listWidget->setUpdatesEnabled(false);
    m_listWidget->clear();
    m_rowWidgets.clear();

    const int count = m_itemModel->rowCount();
    for (int row = 0; row < count; ++row) {
        QWidget *widget = createItemRow(row);

        auto *item = new QListWidgetItem(m_listWidget);
        item->setSizeHint(QSize(0, Theme::kCardHeight));
        m_listWidget->setItemWidget(item, widget);

        const QString itemId =
            m_itemModel->index(row, 0).data(InstallItemModel::ItemIdRole).toString();
        if (!itemId.isEmpty()) {
            auto it = m_rowWidgets.find(itemId);
            if (it != m_rowWidgets.end())
                it->listItem = item;
        }
    }

    m_listWidget->setUpdatesEnabled(true);

    if (count == 0) {
        setStatus(QStringLiteral("当前类别下没有可安装的程序"));
    } else {
        setStatus(QStringLiteral("共 %1 个可安装程序").arg(count));
    }
}

void MainWindow::refreshItemRow(const QString &itemId)
{
    auto it = m_rowWidgets.find(itemId);
    if (it == m_rowWidgets.end())
        return;

    const QString status = m_itemModel->statusOf(itemId);
    const bool installing =
        status == QLatin1String(InstallItemModel::kStatusInstalling);

    if (it->button != nullptr) {
        it->button->setText(statusTextFor(status));
        it->button->setEnabled(!installing && !m_im->isBusy());
        it->button->setStyleSheet(
            status == QLatin1String(InstallItemModel::kStatusFailed)
                ? Theme::retryButtonStyle()
                : Theme::installButtonStyle());
    }

    if (it->progress != nullptr) {
        if (installing) {
            it->progress->setValue(0);
            it->progress->setVisible(true);
        } else {
            it->progress->setVisible(false);
        }
    }

    if (it->stageLabel != nullptr) {
        if (installing) {
            it->stageLabel->setText(QStringLiteral("正在安装..."));
            it->stageLabel->setStyleSheet(
                QStringLiteral("color:%1; font-size:8.5pt;")
                    .arg(QLatin1String(Theme::kPrimary)));
        } else if (status == QLatin1String(InstallItemModel::kStatusDone)) {
            it->stageLabel->setText(QStringLiteral("✓ 安装完成"));
            it->stageLabel->setStyleSheet(
                QStringLiteral("color:%1; font-size:8.5pt;")
                    .arg(QLatin1String(Theme::kSuccess)));
        } else if (status == QLatin1String(InstallItemModel::kStatusFailed)) {
            it->stageLabel->setText(QStringLiteral("✗ 上次安装失败"));
            it->stageLabel->setStyleSheet(
                QStringLiteral("color:%1; font-size:8.5pt;")
                    .arg(QLatin1String(Theme::kDanger)));
        } else {
            it->stageLabel->clear();
        }
    }
}

QWidget *MainWindow::createItemRow(int rowIndex)
{
    const QModelIndex index = m_itemModel->index(rowIndex, 0);
    const QString name        = index.data(InstallItemModel::NameRole).toString();
    const QString version     = index.data(InstallItemModel::VersionRole).toString();
    const QString publisher   = index.data(InstallItemModel::PublisherRole).toString();
    const QString description = index.data(InstallItemModel::DescriptionRole).toString();
    const QString type        = index.data(InstallItemModel::TypeRole).toString();
    const QString itemId      = index.data(InstallItemModel::ItemIdRole).toString();
    const QString status =
        index.data(InstallItemModel::InstallStatusRole).toString();

    // ---------------- 卡片 ----------------
    auto *row = new QWidget;
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto *card = new QFrame(row);
    card->setObjectName(QStringLiteral("itemCard"));
    card->setStyleSheet(QStringLiteral(
        "QFrame#itemCard {"
        "   background:%1;"
        "   border:1px solid %2;"
        "   border-radius:%3px;"
        "}"
        "QFrame#itemCard:hover {"
        "   border-color:%4;"
        "   background:#fbfdff;"
        "}")
        .arg(QLatin1String(Theme::kSurface),
             QLatin1String(Theme::kBorder))
        .arg(Theme::kRadius)
        .arg(QLatin1String(Theme::kPrimary)));
    rowLayout->addWidget(card);

    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(16, 12, 16, 12);
    cardLayout->setSpacing(14);

    // ---------------- 左侧文字 ----------------
    auto *textColumn = new QVBoxLayout;
    textColumn->setSpacing(3);

    // 第一行：名称 + 类型徽标
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);

    auto *nameLabel = new QLabel(name, card);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 1);
    nameLabel->setFont(nameFont);
    nameLabel->setStyleSheet(
        QStringLiteral("color:%1;").arg(QLatin1String(Theme::kTextPrimary)));

    const TypeBadge badge = badgeForType(type);
    // label 是 UTF-8 的 C 字符串，必须 fromUtf8（fromLatin1 会乱码）
    auto *badgeLabel = new QLabel(QString::fromUtf8(badge.label), card);
    badgeLabel->setStyleSheet(
        QStringLiteral(
            "background:%1; color:%2; border-radius:4px;"
            "padding:2px 8px; font-size:8pt; font-weight:bold;")
            .arg(QLatin1String(badge.bg), QLatin1String(badge.fg)));
    badgeLabel->setFixedHeight(20);

    titleRow->addWidget(nameLabel);
    titleRow->addWidget(badgeLabel);
    titleRow->addStretch(1);

    // 第二行：版本 / 发布者
    const QString meta = QStringLiteral("版本 %1   ·   发布者 %2")
                             .arg(version.isEmpty() ? QStringLiteral("—") : version,
                                  publisher.isEmpty() ? QStringLiteral("—") : publisher);
    auto *infoLabel = new QLabel(meta, card);
    infoLabel->setStyleSheet(
        QStringLiteral("color:%1; font-size:8.5pt;")
            .arg(QLatin1String(Theme::kTextSecondary)));

    // 第三行：描述
    auto *descLabel = new QLabel(description, card);
    descLabel->setStyleSheet(
        QStringLiteral("color:%1; font-size:8.5pt;")
            .arg(QLatin1String(Theme::kTextMuted)));

    // 第四行：安装状态文字
    auto *stageLabel = new QLabel(QString(), card);
    stageLabel->setStyleSheet(
        QStringLiteral("color:%1; font-size:8.5pt;")
            .arg(QLatin1String(Theme::kPrimary)));

    // 第五行：进度条
    auto *progress = new QProgressBar(card);
    progress->setRange(0, 100);
    progress->setTextVisible(false);
    progress->setFixedHeight(4);
    progress->setVisible(false);

    textColumn->addLayout(titleRow);
    textColumn->addWidget(infoLabel);
    textColumn->addWidget(descLabel);
    textColumn->addWidget(stageLabel);
    textColumn->addWidget(progress);

    cardLayout->addLayout(textColumn, 1);

    // ---------------- 右侧按钮 ----------------
    auto *button = new QPushButton(statusTextFor(status), card);
    button->setFixedSize(Theme::kButtonWidth, Theme::kButtonHeight);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        status == QLatin1String(InstallItemModel::kStatusFailed)
            ? Theme::retryButtonStyle()
            : Theme::installButtonStyle());
    button->setEnabled(status != QLatin1String(InstallItemModel::kStatusInstalling)
                       && !m_im->isBusy());

    connect(button, &QPushButton::clicked, this,
            [this, itemId]() { onInstallButtonClicked(itemId); });

    cardLayout->addWidget(button, 0, Qt::AlignVCenter);

    // ---------------- 悬停详情 ----------------
    const QString tip = QStringLiteral(
                            "<b>%1</b><br/>"
                            "版本：%2<br/>"
                            "发布者：%3<br/>"
                            "类型：%4<br/>"
                            "标识：%5<br/><br/>%6")
                            .arg(name.toHtmlEscaped(),
                                 version.toHtmlEscaped(),
                                 publisher.toHtmlEscaped(),
                                 type.toHtmlEscaped(),
                                 itemId.toHtmlEscaped(),
                                 description.toHtmlEscaped());
    card->setToolTip(tip);
    row->setToolTip(tip);

    // ---------------- 登记控件，供原地刷新 ----------------
    if (!itemId.isEmpty()) {
        if (status == QLatin1String(InstallItemModel::kStatusInstalling)) {
            progress->setVisible(true);
            stageLabel->setText(QStringLiteral("正在安装..."));
        } else if (status == QLatin1String(InstallItemModel::kStatusDone)) {
            stageLabel->setText(QStringLiteral("✓ 安装完成"));
            stageLabel->setStyleSheet(
                QStringLiteral("color:%1; font-size:8.5pt;")
                    .arg(QLatin1String(Theme::kSuccess)));
        } else if (status == QLatin1String(InstallItemModel::kStatusFailed)) {
            stageLabel->setText(QStringLiteral("✗ 上次安装失败"));
            stageLabel->setStyleSheet(
                QStringLiteral("color:%1; font-size:8.5pt;")
                    .arg(QLatin1String(Theme::kDanger)));
        }

        RowWidgets widgets;
        widgets.button     = button;
        widgets.stageLabel = stageLabel;
        widgets.progress   = progress;
        m_rowWidgets.insert(itemId, widgets);
    }

    return row;
}
