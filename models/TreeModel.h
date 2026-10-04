#ifndef TREEMODEL_H
#define TREEMODEL_H

#include <QAbstractItemModel>
#include <QList>
#include "ConfigManager.h"

class TreeItem
{
public:
    TreeItem(const QString &id, const QString &name, TreeItem *parent = nullptr);
    ~TreeItem();

    void appendChild(TreeItem *child);
    TreeItem *child(int row);
    int childCount() const { return m_children.size(); }
    int row() const;
    TreeItem *parentItem() { return m_parent; }
    QString id() const { return m_id; }
    QString name() const { return m_name; }

private:
    QList<TreeItem*> m_children;
    QString m_id;
    QString m_name;
    TreeItem *m_parent = nullptr;
};

class TreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit TreeModel(QObject *parent = nullptr);
    ~TreeModel();

    enum Roles {
        DisplayRole = Qt::DisplayRole,
        IdRole = Qt::UserRole + 1
    };
    void setConfigManager(ConfigManager *cfg) { m_config = cfg; }
    Q_INVOKABLE void reload();     // ★ 新增，QML 调用无参方法

    Q_INVOKABLE void setCategories(const QList<Category> &categories);

    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation o,
                        int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void setupModelData(const QList<Category> &categories);
    TreeItem *getItem(const QModelIndex &index) const;

    TreeItem *m_rootItem = nullptr;
    ConfigManager *m_config = nullptr;   // ★ 新增
};

#endif
