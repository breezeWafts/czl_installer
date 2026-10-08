#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QString>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QTimer;
class QTreeView;

class ConfigManager;
class DownloadManager;
class InstallManager;
class TreeModel;
class InstallItemModel;
class BookmarkManager;

/**
 * @brief 主窗口：左侧类别树 + 右侧安装卡片列表。
 *
 * 相对旧版的修正：
 *   - 读取树节点 id 时用的角色名写错了（Qt::UserRole+1 = 256，而 TreeModel
 *     声明的 IdRole 在旧版里等于 Qt::DisplayRole，导致类别筛选拿到的始终是
 *     显示文本）；现在统一使用 TreeModel::IdRole；
 *   - 启动时不做任何选中也不加载列表，界面一开始是空的；现在会自动选中
 *     第一个类别并展示其安装项；
 *   - onInstallProgress() 里每次进度更新都清空并重建整份列表（N 个卡片
 *     全部销毁重建，安装 20 次进度就重建 20 次），现在只更新那一个条目的
 *     状态文字与按钮，且用 setUpdatesEnabled(false) 抑制闪烁；
 *   - 删除了槽函数里的 QApplication::processEvents()（重入风险），
 *     因为安装已经跑在后台线程；
 *   - 本地缓存目录从工具栏挪到状态栏，工具栏不再被长路径挤变形。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ConfigManager *config,
                        DownloadManager *downloadManager,
                        BookmarkManager *bookmarkManager,
                        InstallManager *installManager,
                        TreeModel *treeModel,
                        InstallItemModel *itemModel,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onServerTypeChanged(int index);
    void onTreeCurrentChanged(const QModelIndex &current,
                              const QModelIndex &previous);
    void onInstallProgress(const QString &itemId, int percent,
                           const QString &stage);
    void onInstallFinished(const QString &itemId, bool success,
                           const QString &message);
    void onBusyChanged(bool busy);
    void onActiveServerChanged();
    void onResetStatusText();
    void onSetBookmarksClicked();

private:
    void setupUi();
    void setupConnections();

    /// 应用窗口/任务栏图标（优先用 qrc 内嵌的 PNG）。
    void applyWindowIcon();

    /// 生成统一样式的「面板标题条」。
    QWidget *createPanelHeader(const QString &title, const QString &subtitle);

    /// 依据模型内容重建列表（切换类别/服务器时调用）。
    void rebuildItemList();
    /// 只刷新单个条目的状态（安装进度/结果时调用，不重建列表）。
    void refreshItemRow(const QString &itemId);
    /// 创建一个卡片行；同时把相关控件登记进 m_rowWidgets。
    QWidget *createItemRow(int rowIndex);
    void onInstallButtonClicked(const QString &itemId);
    void setStatus(const QString &text, const QString &color = QString());

    /// 每个安装项对应的行内控件。
    struct RowWidgets {
        QListWidgetItem *listItem  = nullptr;
        QPushButton     *button    = nullptr;
        QLabel          *stageLabel = nullptr;
        QProgressBar    *progress  = nullptr;
    };

    // ---------- UI ----------
    QComboBox   *m_serverTypeCombo   = nullptr;
    QLabel      *m_activeServerLabel = nullptr;
    QTreeView   *m_treeView          = nullptr;
    QListWidget *m_listWidget        = nullptr;
    QLabel      *m_statusLabel       = nullptr;
    QLabel      *m_localDirLabel     = nullptr;
    QTimer      *m_statusTimer       = nullptr;
    QPushButton *m_setBookmarksBtn   = nullptr;

    // ---------- 数据（外部注入，不负责释放） ----------
    ConfigManager    *m_config      = nullptr;
    DownloadManager  *m_dl          = nullptr;
    BookmarkManager  *m_bm          = nullptr;
    InstallManager   *m_im          = nullptr;
    TreeModel        *m_treeModel   = nullptr;
    InstallItemModel *m_itemModel   = nullptr;

    QHash<QString, RowWidgets> m_rowWidgets;
    QString m_currentCategoryId;
};

#endif // MAINWINDOW_H
