#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QPushButton>
#include <QMessageBox>

class QComboBox;
class QLabel;
class QTreeView;
class QListWidget;
class QTimer;

class ConfigManager;
class DownloadManager;
class InstallManager;
class TreeModel;
class InstallItemModel;
class BookmarkManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ConfigManager      *cfg,
                        DownloadManager    *dl,
                        BookmarkManager    *bm,
                        InstallManager     *im,
                        TreeModel          *tm,
                        InstallItemModel   *iim,
                        QWidget            *parent = nullptr);
    ~MainWindow();

private slots:
    void onServerTypeChanged(int index);
    void onTreeCurrentChanged(const QModelIndex &current,
                              const QModelIndex &previous);
    void onInstallProgress(const QString &itemId, int percent,
                           const QString &stage);
    void onInstallFinished(const QString &itemId, bool success,
                           const QString &message);
    void onActiveServerChanged();
    void resetStatusText();
    void onSetBookmarksClicked();

private:
    void     setupUi();
    void     setupConnections();
    void     rebuildItemList();
    QWidget *createItemRow(int rowIndex);
    void     onInstallButtonClicked(const QString &itemId);

    // ---------- UI 成员 ----------
    QComboBox   *m_serverTypeCombo  = nullptr;
    QLabel      *m_activeServerLabel = nullptr;
    QLabel      *m_localDirLabel    = nullptr;
    QTreeView   *m_treeView         = nullptr;
    QListWidget *m_listWidget       = nullptr;
    QLabel      *m_statusLabel      = nullptr;
    QTimer      *m_statusTimer      = nullptr;
    QPushButton *m_setBookmarksBtn = nullptr;

    // ---------- 数据成员（外部注入，不负责释放） ----------
    ConfigManager    *m_config    = nullptr;
    DownloadManager  *m_dl        = nullptr;
    BookmarkManager *m_bm = nullptr;   // ★ 由 main.cpp 注入
    InstallManager   *m_im        = nullptr;
    TreeModel        *m_treeModel = nullptr;
    InstallItemModel *m_itemModel = nullptr;


    QString m_currentCategoryId;
};

#endif // MAINWINDOW_H
