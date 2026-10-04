#ifndef INSTALLMANAGER_H
#define INSTALLMANAGER_H

#include <QObject>
#include "ConfigManager.h"

class DownloadManager;
class ProcessManager;
class FileOperationManager;

class InstallManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activeServerName READ activeServerName
               NOTIFY activeServerChanged)
public:
    explicit InstallManager(QObject *parent = nullptr);

    void setDependencies(ConfigManager *cfg,
                         DownloadManager *dl,
                         ProcessManager *pm,
                         FileOperationManager *fm);

    Q_INVOKABLE void installItem(const QString &itemId);
    Q_INVOKABLE void setServerConfigFromQml(const QString &type);

    QString activeServerName() const;

signals:
    void installStarted(const QString &itemId);
    void installProgress(const QString &itemId, int percent, const QString &stage);
    void installFinished(const QString &itemId, bool success, const QString &message);

    void activeServerChanged();

private:
    bool downloadItemToLocal(const InstallItem &item, QString &localPathOut);
    bool processAllSources(const InstallItem &item, int pStart, int pEnd);
    bool processOneSource(const InstallItem &item, const CopySource &src);

    void executeMsi(const InstallItem &item, const QString &localPath);
    void executeExe(const InstallItem &item, const QString &localPath);
    void executeBat(const InstallItem &item, const QString &localPath);

    bool runViaTI(const QString &filePath, const InstallItem &item);
    bool runWithArgsViaTI(const QString &exePath,
                          const QStringList &args,
                          const InstallItem &item);

    ConfigManager *m_config = nullptr;
    DownloadManager *m_downloadManager = nullptr;
    ProcessManager *m_processManager = nullptr;
    FileOperationManager *m_fileManager = nullptr;
};

#endif
