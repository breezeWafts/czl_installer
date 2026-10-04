#ifndef FILEOPERATIONMANAGER_H
#define FILEOPERATIONMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class FileOperationManager : public QObject
{
    Q_OBJECT
public:
    explicit FileOperationManager(QObject *parent = nullptr);

    // 复制文件或目录到目标（覆盖语义）
    bool copyToTarget(const QString &source, const QString &target,
                      bool overwrite = true);

    // 解压 ZIP 到目标目录
    bool extractZip(const QString &zipPath, const QString &destDir,
                    bool overwrite = true);

signals:
    void logMessage(const QString &msg);

private:
    bool copyFile(const QString &src, const QString &dst, bool overwrite);
    bool copyDirectory(const QString &srcDir, const QString &dstDir, bool overwrite);
    int  countEntriesRecursive(const QString &dir);
    bool waitForExtraction(const QString &destDir, int timeoutMs);
};

#endif
