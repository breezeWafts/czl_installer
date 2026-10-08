#ifndef FILEOPERATIONMANAGER_H
#define FILEOPERATIONMANAGER_H

#include <QObject>
#include <QString>

/**
 * @brief 文件复制、目录复制与 ZIP 解压。
 *
 * 相对旧版的修正：
 *   - copyFile() 改为原生分块复制（CreateFileW + ReadFile/WriteFile），
 *     不再依赖 QFile::copy —— 后者在跨卷、目标被占用、需要保留属性时
 *     经常直接失败；
 *   - 复制目标先清除只读属性，避免「无法覆盖只读文件」；
 *   - 解压优先使用 Windows 10+ 自带的 tar.exe（bsdtar 完整支持 zip），
 *     失败才回退到 Shell.Application COM；旧版只有 COM 一条路，
 *     并且用「目录条目数连续 3 次不变」来猜测解压是否结束，
 *     既慢又可能在解压大文件时提前返回。
 */
class FileOperationManager : public QObject
{
    Q_OBJECT

public:
    explicit FileOperationManager(QObject *parent = nullptr);

    /// 复制文件或目录到目标（覆盖语义）。
    bool copyToTarget(const QString &source, const QString &target,
                      bool overwrite = true);

    /// 解压 ZIP 到目标目录。
    bool extractZip(const QString &zipPath, const QString &destDir,
                    bool overwrite = true);

    /// 递归删除目录/文件（原生实现，不依赖 shell）。
    static bool removeRecursively(const QString &path);

    /// 复制失败时是否回退到 TIHelper（受保护目录场景）。
    void setTiFallbackEnabled(bool enabled) { m_tiFallback = enabled; }
    bool isTiFallbackEnabled() const { return m_tiFallback; }

signals:
    void logMessage(const QString &message);

private:
    bool copyFileNative(const QString &src, const QString &dst, bool overwrite);
    bool copyDirectory(const QString &srcDir, const QString &dstDir, bool overwrite);

    /// 通过 TIHelper 以 TrustedInstaller 权限复制单文件。
    bool copyFileViaTrustedInstaller(const QString &src, const QString &dst);

    /// 优先：Win10+ 自带 tar.exe。
    bool extractZipWithTar(const QString &zipPath, const QString &destDir);
    /// 回退：Shell.Application COM。
    bool extractZipWithShell(const QString &zipPath, const QString &destDir);

    void reportError(const QString &message);

    bool m_tiFallback = true;
};

#endif // FILEOPERATIONMANAGER_H
