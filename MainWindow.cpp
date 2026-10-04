#include "MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "DownloadManager.h"
#include "InstallManager.h"
#include "TreeModel.h"
#include "InstallItemModel.h"
#include "BookmarkManager.h"
#include "Logger.h"

// ============================================================
// 构造 / 析构
// ============================================================
MainWindow::MainWindow(ConfigManager      *cfg,
                       DownloadManager    *dl,
                       BookmarkManager    *bm,
                       InstallManager     *im,
                       TreeModel          *tm,
                       InstallItemModel   *iim,
                       QWidget            *parent)
    : QMainWindow(parent)
    , m_config(cfg)
    , m_dl(dl)
    , m_bm(bm)
    , m_im(im)
    , m_treeModel(tm)
    , m_itemModel(iim)
{
    setWindowTitle("czl_installer - 软件安装管理器");
    resize(1200, 750);

    setupUi();
    setupConnections();

    // 启动时同步一下服务器名显示
    onActiveServerChanged();
}

MainWindow::~MainWindow() {}

// ============================================================
// UI 构建
// ============================================================
void MainWindow::setupUi()
{
    // ---------------- 顶部工具栏 ----------------
    QWidget *toolbar = new QWidget(this);
    toolbar->setStyleSheet("background:#f5f5f5; border-bottom:1px solid #ddd;");
    QHBoxLayout *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(12, 6, 12, 6);
    tbLayout->setSpacing(12);

    QLabel *serverLabel = new QLabel("服务器方式:");
    QFont boldFont = serverLabel->font();
    boldFont.setBold(true);
    serverLabel->setFont(boldFont);

    m_serverTypeCombo = new QComboBox;
    m_serverTypeCombo->addItem("FTP (多地址备选)");
    m_serverTypeCombo->addItem("HTTP (多地址备选)");
    m_serverTypeCombo->setCurrentIndex(0);
    m_serverTypeCombo->setMinimumWidth(180);

    tbLayout->addWidget(serverLabel);
    tbLayout->addWidget(m_serverTypeCombo);
    tbLayout->addStretch();

    m_activeServerLabel = new QLabel("当前服务器: 未连接");
    m_activeServerLabel->setStyleSheet("color:#999; font-weight:bold;");
    tbLayout->addWidget(m_activeServerLabel);

    m_localDirLabel = new QLabel("本地缓存: " + m_config->localBaseDir());
    m_localDirLabel->setStyleSheet("color:#666;");
    // ★ 设置浏览器书签按钮
    m_setBookmarksBtn = new QPushButton("设置浏览器书签");
    m_setBookmarksBtn->setMinimumHeight(28);
    m_setBookmarksBtn->setStyleSheet(
        "QPushButton {"
        "   background:#4a90d9; color:white;"
        "   border:none; border-radius:4px;"
        "   padding:4px 14px; font-weight:bold;"
        "}"
        "QPushButton:hover { background:#5aa0e9; }"
        "QPushButton:pressed { background:#3a7bc0; }");

    tbLayout->addWidget(m_setBookmarksBtn);
    tbLayout->addWidget(m_localDirLabel);


    // ---------------- 主体：QSplitter 左树右列表 ----------------
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    // ===== 左侧：类别树 =====
    QWidget *leftPanel = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    QLabel *treeTitle = new QLabel("软件类别");
    treeTitle->setAlignment(Qt::AlignCenter);
    treeTitle->setStyleSheet(
        "background:#f0f0f0; padding:10px; "
        "font-weight:bold; border-bottom:1px solid #ddd;");
    leftLayout->addWidget(treeTitle);

    m_treeView = new QTreeView;
    m_treeView->setModel(m_treeModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setAnimated(true);
    m_treeView->setStyleSheet(
        "QTreeView { padding:4px; }"
        "QTreeView::item { padding:4px 6px; }"
        "QTreeView::item:selected { background:#d0e8ff; color:#1a4a80; }"
        "QTreeView::item:hover { background:#eaf3ff; }");
    m_treeView->expandAll();

    leftLayout->addWidget(m_treeView);
    leftPanel->setMinimumWidth(200);
    leftPanel->setMaximumWidth(400);

    // ===== 右侧：可安装程序列表 =====
    QWidget *rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    QLabel *listTitle = new QLabel("可安装程序列表");
    listTitle->setAlignment(Qt::AlignCenter);
    listTitle->setStyleSheet(
        "background:#f0f0f0; padding:10px; "
        "font-weight:bold; border-bottom:1px solid #ddd;");
    rightLayout->addWidget(listTitle);

    m_listWidget = new QListWidget;
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listWidget->setSpacing(6);
    m_listWidget->setStyleSheet("QListWidget { background:#fafafa; border:none; "
                                "padding:6px; }");
    m_listWidget->setMouseTracking(true);
    rightLayout->addWidget(m_listWidget);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 280 << 920);

    // ---------------- 整体布局 ----------------
    QWidget *central = new QWidget;
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(toolbar);
    mainLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    // ---------------- 底部状态栏 ----------------
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("padding-left:8px;");
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->setStyleSheet(
        "QStatusBar { background:#f8f8f8; border-top:1px solid #e0e0e0; }");

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(5000);
    connect(m_statusTimer, &QTimer::timeout,
            this, &MainWindow::resetStatusText);
}

// ============================================================
// 信号连接
// ============================================================
void MainWindow::setupConnections()
{
    // 服务器类型切换
    connect(m_serverTypeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onServerTypeChanged);

    // 树选中项变化
    connect(m_treeView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, &MainWindow::onTreeCurrentChanged);
    connect(m_setBookmarksBtn, &QPushButton::clicked,
            this, &MainWindow::onSetBookmarksClicked);

    // 安装流程信号
    connect(m_im, &InstallManager::installProgress,
            this, &MainWindow::onInstallProgress);
    connect(m_im, &InstallManager::installFinished,
            this, &MainWindow::onInstallFinished);
    connect(m_im, &InstallManager::activeServerChanged,
            this, &MainWindow::onActiveServerChanged);
}

// ============================================================
// 槽函数
// ============================================================
void MainWindow::onServerTypeChanged(int index)
{
    QString t = (index == 0) ? "ftp" : "http";
    Logger::info(QString("切换服务器方式为: %1").arg(t));
    m_im->setServerConfigFromQml(t);

    // 重新加载树
    m_treeModel->reload();
    m_treeView->expandAll();

    // 重新加载列表
    m_itemModel->loadAll();
    rebuildItemList();

    m_localDirLabel->setText("本地缓存: " + m_config->localBaseDir());
    onActiveServerChanged();
}

void MainWindow::onTreeCurrentChanged(const QModelIndex &current,
                                      const QModelIndex &previous)
{
    Q_UNUSED(previous);
    if (!current.isValid()) return;

    QString catId = m_treeModel->data(current, TreeModel::IdRole).toString();
    if (catId.isEmpty()) return;

    m_currentCategoryId = catId;
    Logger::info(QString("选择类别: %1").arg(catId));
    m_itemModel->filterByCategory(catId);
    rebuildItemList();
}

void MainWindow::onInstallButtonClicked(const QString &itemId)
{
    Logger::info(QString("用户点击安装: %1").arg(itemId));
    m_itemModel->setStatus(itemId, "installing");
    rebuildItemList();      // 立即把按钮刷成"安装中..."

    m_im->installItem(itemId);
}

void MainWindow::onInstallProgress(const QString &itemId, int percent,
                                   const QString &stage)
{
    Q_UNUSED(itemId);
    m_statusLabel->setText(QString("[%1%] %2").arg(percent).arg(stage));
    m_statusLabel->setStyleSheet("color:#333; padding-left:8px;");
    QApplication::processEvents();
}

void MainWindow::onInstallFinished(const QString &itemId, bool success,
                                   const QString &message)
{
    if (success)
            Logger::info (QString("安装成功 [%1]: %2").arg(itemId, message));  // ★
        else
            Logger::error(QString("安装失败 [%1]: %2").arg(itemId, message));  // ★
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(
        QString("color:%1; padding-left:8px;")
            .arg(success ? "#2b7a2b" : "#c00"));

    m_itemModel->setStatus(itemId, success ? "done" : "failed");
    rebuildItemList();
    m_statusTimer->start();
}

void MainWindow::onActiveServerChanged()
{
    QString name = m_dl->activeServerName();
    if (name.isEmpty()) {
        m_activeServerLabel->setText("当前服务器: 未连接");
        m_activeServerLabel->setStyleSheet("color:#999; font-weight:bold;");
    } else {
        m_activeServerLabel->setText("当前服务器: " + name);
        m_activeServerLabel->setStyleSheet("color:#2b7a2b; font-weight:bold;");
    }
}

void MainWindow::resetStatusText()
{
    m_statusLabel->setText("就绪");
    m_statusLabel->setStyleSheet("color:black; padding-left:8px;");
}

void MainWindow::onSetBookmarksClicked()
{
    if (!m_bm) return;

    const QList<BookmarkItem> items = m_config->bookmarks();
    if (items.isEmpty()) {
        Logger::warn("设置浏览器书签：配置文件中没有书签项");
        QMessageBox::information(this, "设置浏览器书签",
            "配置文件中没有书签项（bookmarks 数组为空）。");
        return;
    }
    Logger::info(QString("用户点击设置浏览器书签，共 %1 条")        // ★
                        .arg(items.size()));

    if (QMessageBox::question(this, "设置浏览器书签",
            QString("即将为所有已安装的浏览器设置 %1 条书签。\n\n"
                    "执行期间相关浏览器会被强制关闭，是否继续？")
                .arg(items.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes) != QMessageBox::Yes) {
        Logger::info("用户取消设置浏览器书签");                     // ★
        return;
    }

    m_setBookmarksBtn->setEnabled(false);
    m_statusLabel->setText("正在设置浏览器书签...");
    m_statusLabel->setStyleSheet("color:#333; padding-left:8px;");
    QApplication::processEvents();

    BookmarkManager::Result r = m_bm->applyToAll(items);
    Logger::info(QString("设置浏览器书签完成: %1").arg(r.message)); // ★

    QString title = r.ok ? "设置完成" : "设置结果";
    QString detail;
    if (!r.browsers.isEmpty())
        detail += "成功: " + r.browsers.join(", ") + "\n";
    if (!r.failed.isEmpty())
        detail += "失败: " + r.failed.join(", ") + "\n";
    detail += "\n" + r.message;

    QMessageBox::information(this, title, detail);

    m_setBookmarksBtn->setEnabled(true);
    m_statusLabel->setText(r.message);
    m_statusLabel->setStyleSheet(
        QString("color:%1; padding-left:8px;")
            .arg(r.ok ? "#2b7a2b" : "#c00"));
    m_statusTimer->start();
}

// ============================================================
// 列表重建
// ============================================================
void MainWindow::rebuildItemList()
{
    m_listWidget->clear();

    const int n = m_itemModel->rowCount();
    for (int i = 0; i < n; ++i) {
        QWidget *row = createItemRow(i);

        QListWidgetItem *item = new QListWidgetItem(m_listWidget);
        item->setSizeHint(QSize(0, 90));
        m_listWidget->setItemWidget(item, row);
    }
}

// ============================================================
// 单行布局
// ============================================================
QWidget *MainWindow::createItemRow(int rowIndex)
{
    QModelIndex idx = m_itemModel->index(rowIndex, 0);

    const QString name      = idx.data(InstallItemModel::NameRole).toString();
    const QString version   = idx.data(InstallItemModel::VersionRole).toString();
    const QString publisher = idx.data(InstallItemModel::PublisherRole).toString();
    const QString desc      = idx.data(InstallItemModel::DescriptionRole).toString();
    const QString type      = idx.data(InstallItemModel::TypeRole).toString();
    const QString itemId    = idx.data(InstallItemModel::ItemIdRole).toString();
    const QString status    = idx.data(InstallItemModel::InstallStatusRole).toString();

    // ---------------- 外层：给条目留出边距 ----------------
    QWidget *row = new QWidget;

    // 卡片
    QFrame *frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(
        "QFrame { background:#ffffff; border:1px solid #ddd; "
        "        border-radius:6px; }"
        "QFrame:hover { border-color:#4a90d9; background:#f5faff; }");

    QHBoxLayout *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(6, 3, 6, 3);
    rowLayout->addWidget(frame);

    // ---------------- 卡片内：左信息 + 右按钮 ----------------
    QHBoxLayout *card = new QHBoxLayout(frame);
    card->setContentsMargins(14, 10, 14, 10);
    card->setSpacing(12);

    // 左侧文字
    QVBoxLayout *textCol = new QVBoxLayout;
    textCol->setSpacing(2);

    QLabel *nameLabel = new QLabel(name);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 1);
    nameLabel->setFont(nameFont);

    QLabel *infoLabel = new QLabel(QString("版本: %1  |  发布者: %2")
        .arg(version.isEmpty()   ? "-" : version,
             publisher.isEmpty() ? "-" : publisher));
    infoLabel->setStyleSheet("color:#666; font-size:9pt;");

    QLabel *descLabel = new QLabel(desc);
    descLabel->setStyleSheet("color:#888; font-size:9pt;");
    descLabel->setWordWrap(false);

    textCol->addWidget(nameLabel);
    textCol->addWidget(infoLabel);
    textCol->addWidget(descLabel);

    card->addLayout(textCol, 1);

    // 右侧按钮
    QPushButton *btn = new QPushButton;
    btn->setMinimumWidth(96);
    btn->setMinimumHeight(34);

    if (status == "installing") {
        btn->setText("安装中...");
        btn->setEnabled(false);
    } else if (status == "done") {
        btn->setText("已安装");
    } else if (status == "failed") {
        btn->setText("重试");
    } else {
        btn->setText("安装");
    }

    btn->setStyleSheet(
        "QPushButton {"
        "   background:#2b7a2b; color:white;"
        "   border:none; border-radius:4px;"
        "   padding:6px 16px; font-weight:bold;"
        "}"
        "QPushButton:hover { background:#359035; }"
        "QPushButton:pressed { background:#1f5a1f; }"
        "QPushButton:disabled { background:#aaaaaa; }");

    connect(btn, &QPushButton::clicked, this, [this, itemId]() {
        onInstallButtonClicked(itemId);
    });

    card->addWidget(btn, 0, Qt::AlignVCenter);

    // ---------------- 悬停详情（tooltip） ----------------
    const QString tip = QString(
        "<b>%1</b><br/>"
        "版本: %2<br/>"
        "发布者: %3<br/>"
        "类型: %4<br/><br/>"
        "%5")
        .arg(name.toHtmlEscaped(),
             version.toHtmlEscaped(),
             publisher.toHtmlEscaped(),
             type.toHtmlEscaped(),
             desc.toHtmlEscaped());

    frame->setToolTip(tip);
    row->setToolTip(tip);

    return row;
}
