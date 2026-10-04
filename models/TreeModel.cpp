#include "TreeModel.h"

// ============================================================
// TreeItem
// ============================================================
TreeItem::TreeItem(const QString &id, const QString &name, TreeItem *parent)
    : m_id(id), m_name(name), m_parent(parent) {}

TreeItem::~TreeItem() { qDeleteAll(m_children); }

void TreeItem::appendChild(TreeItem *child) { m_children.append(child); }

TreeItem *TreeItem::child(int row)
{
    if (row < 0 || row >= m_children.size()) return nullptr;
    return m_children.at(row);
}

int TreeItem::row() const
{
    if (m_parent)
        return m_parent->m_children.indexOf(const_cast<TreeItem*>(this));
    return 0;
}

// ============================================================
// TreeModel
// ============================================================
TreeModel::TreeModel(QObject *parent)
    : QAbstractItemModel(parent), m_rootItem(nullptr) {}

TreeModel::~TreeModel() { delete m_rootItem; }

void TreeModel::setCategories(const QList<Category> &categories)
{
    beginResetModel();
    delete m_rootItem;
    m_rootItem = new TreeItem("root", "Root");
    setupModelData(categories);
    endResetModel();
}

void TreeModel::setupModelData(const QList<Category> &categories)
{
    std::function<void(const Category&, TreeItem*)> addCategory =
        [&](const Category &c, TreeItem *parent) {
        TreeItem *item = new TreeItem(c.id, c.name, parent);
        parent->appendChild(item);
        for (const auto &child : c.children)
            addCategory(child, item);
    };

    for (const auto &cat : categories)
        addCategory(cat, m_rootItem);
}

QModelIndex TreeModel::index(int row, int column,
                              const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) return QModelIndex();
    TreeItem *p = getItem(parent);
    TreeItem *c = p->child(row);
    if (c) return createIndex(row, column, c);
    return QModelIndex();
}

QModelIndex TreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) return QModelIndex();
    TreeItem *c = getItem(index);
    TreeItem *p = c->parentItem();
    if (!p || p == m_rootItem) return QModelIndex();
    return createIndex(p->row(), 0, p);
}

int TreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) return 0;
    return getItem(parent)->childCount();
}

int TreeModel::columnCount(const QModelIndex &) const { return 1; }

QVariant TreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    TreeItem *item = getItem(index);

    switch (role) {
    case Qt::DisplayRole: return item->name();
    case IdRole:          return item->id();
    default: return QVariant();
    }
}

Qt::ItemFlags TreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant TreeModel::headerData(int, Qt::Orientation, int) const { return QVariant(); }

QHash<int, QByteArray> TreeModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[IdRole]          = "nodeId";
    return roles;
}

TreeItem *TreeModel::getItem(const QModelIndex &index) const
{
    if (index.isValid()) {
        TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
        if (item) return item;
    }
    return m_rootItem;
}

void TreeModel::reload()
{
    if (m_config)
        setCategories(m_config->categories());
}
