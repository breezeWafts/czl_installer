#include "InstallItemModel.h"

InstallItemModel::InstallItemModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int InstallItemModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant InstallItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case VersionRole:
        return row.version;
    case PublisherRole:
        return row.publisher;
    case DescriptionRole:
        return row.description;
    case TypeRole:
        return row.type;
    case ItemIdRole:
        return row.itemId;
    case InstallStatusRole:
        // 状态存在 m_status 里，而不是行结构体里 —— 这样刷新列表也不丢状态。
        return m_status.value(row.itemId, QString::fromLatin1(kStatusIdle));
    default:
        return {};
    }
}

QHash<int, QByteArray> InstallItemModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[NameRole]          = QByteArrayLiteral("name");
    roles[VersionRole]       = QByteArrayLiteral("version");
    roles[PublisherRole]     = QByteArrayLiteral("publisher");
    roles[DescriptionRole]   = QByteArrayLiteral("description");
    roles[TypeRole]          = QByteArrayLiteral("type");
    roles[ItemIdRole]        = QByteArrayLiteral("itemId");
    roles[InstallStatusRole] = QByteArrayLiteral("installStatus");
    return roles;
}

void InstallItemModel::addItem(const InstallItem &item)
{
    Row row;
    row.name        = item.name;
    row.version     = item.version;
    row.publisher   = item.publisher;
    row.description = item.description;
    row.type        = item.type;
    row.itemId      = item.id;

    m_rows.append(row);
    m_items.append(item);

    // 首次见到该项目时初始化状态，已存在的状态原样保留。
    if (!m_status.contains(item.id))
        m_status.insert(item.id, QString::fromLatin1(kStatusIdle));
}

void InstallItemModel::appendItemsFromCategory(const Category &category)
{
    for (const InstallItem &item : category.items)
        addItem(item);
    for (const Category &child : category.children)
        appendItemsFromCategory(child);
}

void InstallItemModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    m_items.clear();

    if (m_config == nullptr) {
        endResetModel();
        return;
    }

    if (m_categoryId.isEmpty()) {
        // 「全部」：遍历整棵类别树
        for (const Category &category : m_config->categories())
            appendItemsFromCategory(category);
    } else {
        // 指定类别：包含它自己以及所有后代的安装项。
        // 旧实现只取 cat.items 与 children，行为基本一致但没有做去重，
        // 这里改用递归收集，语义更清晰。
        Category root;
        if (m_config->findCategory(m_categoryId, root)) {
            for (const InstallItem &item : root.items)
                addItem(item);
            for (const Category &child : root.children)
                appendItemsFromCategory(child);
        }
    }

    endResetModel();
}

void InstallItemModel::loadAll()
{
    m_categoryId.clear();
    rebuild();
}

void InstallItemModel::filterByCategory(const QString &categoryId)
{
    m_categoryId = categoryId;
    rebuild();
}

void InstallItemModel::setStatus(const QString &itemId, const QString &status)
{
    if (itemId.isEmpty())
        return;

    const QString previous = m_status.value(itemId);
    if (previous == status)
        return;

    m_status.insert(itemId, status);

    // 只有当前列表里存在该项时才需要发信号
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).itemId != itemId)
            continue;

        const QModelIndex modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex, {InstallStatusRole});
        break;
    }
}

QString InstallItemModel::statusOf(const QString &itemId) const
{
    return m_status.value(itemId, QString::fromLatin1(kStatusIdle));
}

bool InstallItemModel::itemAt(int row, InstallItem &out) const
{
    if (row < 0 || row >= m_items.size())
        return false;
    out = m_items.at(row);
    return true;
}
