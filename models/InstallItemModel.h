#ifndef INSTALLITEMMODEL_H
#define INSTALLITEMMODEL_H

#include <QAbstractListModel>
#include <QList>
#include "ConfigManager.h"

class InstallItemModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit InstallItemModel(QObject *parent = nullptr);

    enum Roles {
        NameRole = Qt::UserRole + 1,
        VersionRole,
        PublisherRole,
        DescriptionRole,
        TypeRole,
        ItemIdRole,
        InstallStatusRole
    };

    void setConfigManager(ConfigManager *cfg) { m_config = cfg; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void loadAll();
    Q_INVOKABLE void filterByCategory(const QString &categoryId);
    Q_INVOKABLE void setStatus(const QString &itemId, const QString &status);

private:
    struct Row {
        QString name;
        QString version;
        QString publisher;
        QString description;
        QString type;
        QString itemId;
        QString installStatus;
    };

    void appendItemsFromCategory(const Category &cat);
    bool findCategory(const QString &id, Category &out) const;
    void addItem(const InstallItem &it);

    QList<Row> m_rows;
    ConfigManager *m_config = nullptr;
};

#endif
