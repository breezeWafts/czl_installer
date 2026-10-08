#include "TreeModel.h"

#include <utility>

// ===========================================================================
// TreeItem
// ===========================================================================
TreeItem::TreeItem(QString id, QString name, TreeItem *parent)
    : m_id(std::move(id))
    , m_name(std::move(name))
    , m_parent(parent)
{
}

TreeItem::~TreeItem()
{
    qDeleteAll(m_children);
    m_children.clear();
}

void TreeItem::appendChild(TreeItem *child)
{
    if (child != nullptr)
        m_children.append(child);
}

TreeItem *TreeItem::child(int row) const
{
    if (row < 0 || row >= m_children.size())
        return nullptr;
    return m_children.at(row);
}

int TreeItem::row() const
{
    if (m_parent != nullptr)
        return static_cast<int>(m_parent->m_children.indexOf(
            const_cast<TreeItem *>(this)));
    return 0;
}

// ===========================================================================
// TreeModel
// ===========================================================================
TreeModel::TreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_rootItem(new TreeItem(QStringLiteral("__root__"), QString()))
{
    // 旧版把 m_rootItem 初始化为 nullptr，随后 index()/rowCount() 会通过
    // getItem() 解引用空指针 —— 只要模型在 setCategories() 之前被视图访问
    // 就会崩溃。这里预先建好根节点，彻底消除该隐患。
}

TreeModel::~TreeModel()
{
    delete m_rootItem;
    m_rootItem = nullptr;
}

void TreeModel::setCategories(const QList<Category> &categories)
{
    beginResetModel();
    delete m_rootItem;
    m_rootItem = new TreeItem(QStringLiteral("__root__"), QString());
    setupModelData(categories);
    endResetModel();
}

void TreeModel::setupModelData(const QList<Category> &categories)
{
    // 旧版用 std::function 递归 lambda 却没有 #include <functional>，
    // 在部分标准库实现下直接编译失败。改用显式栈，零额外依赖。
    struct Frame {
        const Category *category;
        TreeItem       *parent;
    };

    QList<Frame> stack;
    stack.reserve(categories.size());
    for (const Category &category : categories)
        stack.append({&category, m_rootItem});

    while (!stack.isEmpty()) {
        const Frame frame = stack.takeLast();
        const Category &category = *frame.category;

        auto *node = new TreeItem(category.id, category.name, frame.parent);
        frame.parent->appendChild(node);

        // 逆序压栈，配合 takeLast() 保证同级节点按配置顺序出现
        for (auto it = category.children.crbegin();
             it != category.children.crend(); ++it) {
            stack.append({&(*it), node});
        }
    }
}

QModelIndex TreeModel::index(int row, int column,
                             const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    TreeItem *parentItem = itemFromIndex(parent);
    TreeItem *childItem  = parentItem->child(row);
    if (childItem == nullptr)
        return {};

    return createIndex(row, column, childItem);
}

QModelIndex TreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    TreeItem *childItem  = itemFromIndex(index);
    TreeItem *parentItem = childItem->parentItem();
    if (parentItem == nullptr || parentItem == m_rootItem)
        return {};

    return createIndex(parentItem->row(), 0, parentItem);
}

int TreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;

    const TreeItem *item = itemFromIndex(parent);
    return item != nullptr ? item->childCount() : 0;
}

int TreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant TreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const TreeItem *item = itemFromIndex(index);
    if (item == nullptr)
        return {};

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return item->name();
    case Qt::ToolTipRole:
        return item->name();
    case IdRole:
        return item->id();
    default:
        return {};
    }
}

Qt::ItemFlags TreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant TreeModel::headerData(int section, Qt::Orientation orientation,
                              int role) const
{
    Q_UNUSED(section)
    Q_UNUSED(orientation)
    Q_UNUSED(role)
    return {};
}

QHash<int, QByteArray> TreeModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
    roles[IdRole] = QByteArrayLiteral("nodeId");
    return roles;
}

QModelIndex TreeModel::firstLeafOrTopIndex() const
{
    if (m_rootItem == nullptr || m_rootItem->childCount() == 0)
        return {};
    return index(0, 0, QModelIndex());
}

TreeItem *TreeModel::itemFromIndex(const QModelIndex &index) const
{
    if (index.isValid()) {
        if (auto *item = static_cast<TreeItem *>(index.internalPointer()))
            return item;
    }
    return m_rootItem;
}

void TreeModel::reload()
{
    if (m_config != nullptr)
        setCategories(m_config->categories());
    else
        setCategories({});
}
