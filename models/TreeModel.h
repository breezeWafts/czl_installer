#ifndef TREEMODEL_H
#define TREEMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QString>

#include "ConfigManager.h"

/**
 * @brief 类别树的一个节点。
 *
 * 使用父子指针的经典实现；析构时递归释放子节点，根节点由模型持有。
 */
class TreeItem
{
public:
    TreeItem(QString id, QString name, TreeItem *parent = nullptr);
    ~TreeItem();

    TreeItem(const TreeItem &) = delete;
    TreeItem &operator=(const TreeItem &) = delete;

    void appendChild(TreeItem *child);

    TreeItem *child(int row) const;
    int       childCount() const { return static_cast<int>(m_children.size()); }
    int       row() const;

    TreeItem *parentItem() const { return m_parent; }

    const QString &id() const { return m_id; }
    const QString &name() const { return m_name; }

private:
    QList<TreeItem *> m_children;
    QString           m_id;
    QString           m_name;
    TreeItem         *m_parent = nullptr;
};

/**
 * @brief 把 ConfigManager 的 Category 树暴露成只读的 QAbstractItemModel。
 */
class TreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1   ///< 节点 id，供 MainWindow 过滤列表使用
    };
    Q_ENUM(Role)

    explicit TreeModel(QObject *parent = nullptr);
    ~TreeModel() override;

    void setConfigManager(ConfigManager *config) { m_config = config; }

    /// 从 ConfigManager 重新构建整棵树。
    Q_INVOKABLE void reload();
    /// 直接用给定类别构建（便于测试）。
    Q_INVOKABLE void setCategories(const QList<Category> &categories);

    // --- QAbstractItemModel ---
    QVariant      data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole) const override;
    QModelIndex   index(int row, int column,
                        const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex   parent(const QModelIndex &index) const override;
    int           rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int           columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 返回第一个节点的索引（方便 UI 默认选中一项）。
    QModelIndex firstLeafOrTopIndex() const;

private:
    void      setupModelData(const QList<Category> &categories);
    TreeItem *itemFromIndex(const QModelIndex &index) const;

    TreeItem      *m_rootItem = nullptr;
    ConfigManager *m_config   = nullptr;
};

#endif // TREEMODEL_H
