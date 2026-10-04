#include "InstallItemModel.h"

InstallItemModel::InstallItemModel(QObject *parent) : QAbstractListModel(parent) {}

int InstallItemModel::rowCount(const QModelIndex &) const { return m_rows.size(); }

QVariant InstallItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return QVariant();
    const Row &r = m_rows[index.row()];
    switch (role) {
    case NameRole:          return r.name;
    case VersionRole:       return r.version;
    case PublisherRole:     return r.publisher;
    case DescriptionRole:   return r.description;
    case TypeRole:          return r.type;
    case ItemIdRole:        return r.itemId;
    case InstallStatusRole: return r.installStatus;
    default:                return QVariant();
    }
}

QHash<int, QByteArray> InstallItemModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole]          = "name";
    roles[VersionRole]       = "version";
    roles[PublisherRole]     = "publisher";
    roles[DescriptionRole]   = "description";
    roles[TypeRole]          = "type";
    roles[ItemIdRole]        = "itemId";
    roles[InstallStatusRole] = "installStatus";
    return roles;
}

void InstallItemModel::addItem(const InstallItem &it)
{
    Row r;
    r.name          = it.name;
    r.version       = it.version;
    r.publisher     = it.publisher;
    r.description   = it.description;
    r.type          = it.type;
    r.itemId        = it.id;
    r.installStatus = "idle";
    m_rows.append(r);
}

void InstallItemModel::appendItemsFromCategory(const Category &cat)
{
    for (const auto &it : cat.items) addItem(it);
    for (const auto &child : cat.children) appendItemsFromCategory(child);
}

void InstallItemModel::loadAll()
{
    if (!m_config) return;
    beginResetModel();
    m_rows.clear();
    for (const auto &cat : m_config->categories())
        appendItemsFromCategory(cat);
    endResetModel();
}

bool InstallItemModel::findCategory(const QString &id, Category &out) const
{
    if (!m_config) return false;
    std::function<bool(const QList<Category>&)> walk =
        [&](const QList<Category> &cats) -> bool {
        for (const auto &c : cats) {
            if (c.id == id) { out = c; return true; }
            if (walk(c.children)) return true;
        }
        return false;
    };
    return walk(m_config->categories());
}

void InstallItemModel::filterByCategory(const QString &categoryId)
{
    if (!m_config) return;
    beginResetModel();
    m_rows.clear();

    Category cat;
    if (findCategory(categoryId, cat)) {
        for (const auto &it : cat.items) addItem(it);
        for (const auto &child : cat.children) appendItemsFromCategory(child);
    }
    endResetModel();
}

void InstallItemModel::setStatus(const QString &itemId, const QString &status)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].itemId == itemId) {
            m_rows[i].installStatus = status;
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {InstallStatusRole});
            return;
        }
    }
}
