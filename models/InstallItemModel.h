#ifndef INSTALLITEMMODEL_H
#define INSTALLITEMMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>

#include "ConfigManager.h"

/**
 * @brief 当前列表中展示的安装项，供 MainWindow 以「卡片 + 安装按钮」渲染。
 *
 * 关键修正：
 *   - setStatus() 会发出 dataChanged 并在内部维护 _id -> status 映射，
 *     切换类别/服务器后安装状态不再丢失（旧版每次 loadAll 都重置为 idle）；
 *   - filterByCategory() 递归收集后代安装项，选中父类别不再是空列表。
 */
class InstallItemModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        VersionRole,
        PublisherRole,
        DescriptionRole,
        TypeRole,
        ItemIdRole,
        InstallStatusRole
    };
    Q_ENUM(Role)

    /// 安装状态取值（字符串形式，便于直接落盘/显示）。
    static constexpr const char *kStatusIdle       = "idle";
    static constexpr const char *kStatusInstalling = "installing";
    static constexpr const char *kStatusDone       = "done";
    static constexpr const char *kStatusFailed     = "failed";

    explicit InstallItemModel(QObject *parent = nullptr);

    void setConfigManager(ConfigManager *config) { m_config = config; }

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 加载全部安装项（不再清空安装状态）。
    Q_INVOKABLE void loadAll();
    /// 只展示指定类别（含其后代）的安装项。
    Q_INVOKABLE void filterByCategory(const QString &categoryId);
    /// 更新某个安装项的状态，状态会被记住。
    Q_INVOKABLE void setStatus(const QString &itemId, const QString &status);

    /// 读取某个安装项当前状态。
    QString statusOf(const QString &itemId) const;

    /// 读取第 row 行的完整 InstallItem（用于安装时取参数）。
    bool itemAt(int row, InstallItem &out) const;

    /// id -> 状态 的快照。
    const QHash<QString, QString> &statusMap() const { return m_status; }

private:
    struct Row {
        QString name;
        QString version;
        QString publisher;
        QString description;
        QString type;
        QString itemId;
    };

    void addItem(const InstallItem &item);
    /// 递归收集，并保留传入 categoryId 作为列表归属标记。
    void appendItemsFromCategory(const Category &category);

    void rebuild();

    QList<Row>              m_rows;
    QList<InstallItem>      m_items;      ///< 与 m_rows 一一对应
    QHash<QString, QString> m_status;     ///< 跨刷新保留的安装状态
    ConfigManager          *m_config  = nullptr;
    QString                 m_categoryId;  ///< 空表示「全部」
};

#endif // INSTALLITEMMODEL_H
