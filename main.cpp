#include <QApplication>
#include <QMessageBox>
#include <QDebug>

#include "ConfigManager.h"
#include "DownloadManager.h"
#include "ProcessManager.h"
#include "FileOperationManager.h"
#include "InstallManager.h"
#include "TIHelperWrapper.h"
#include "TreeModel.h"
#include "InstallItemModel.h"
#include "MainWindow.h"
#include "BookmarkManager.h"
#include "Logger.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("czl_installer");
    app.setOrganizationName("CZL");

    // ---------- 1. 先加载配置（后续日志要依赖 localBaseDir） ----------
    ConfigManager configManager;
    QString configFile = "config/config_ftp.json";
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--http")      configFile = "config/config_http.json";
        else if (arg == "--ftp")  configFile = "config/config_ftp.json";
    }
    bool loaded = configManager.loadConfig(configFile);

    // ---------- 2. ★ 初始化日志系统 ----------
    //   日志目录 = <localBaseDir>/log，即 D:/c_temp/log
    Logger::instance().init(configManager.localBaseDir());

    if (!loaded) {
        qWarning() << "初始配置加载失败:" << configFile;
        QMessageBox::warning(nullptr, "警告",
            "初始配置加载失败:\n" + configFile);
    }

    Logger::info(QString("配置文件: %1").arg(configFile));
    Logger::info(QString("本地缓存目录: %1").arg(configManager.localBaseDir()));

    // ---------- 3. TIHelper.dll ----------
    if (!TIHelperWrapper::instance().load()) {
        qWarning() << "TIHelper.dll 加载失败，TrustedInstaller 提权不可用";
    }

    // ---------- 4. 初始化各管理器 ----------

    if (!configManager.loadConfig(configFile)) {
        QMessageBox::warning(nullptr, "警告",
            "初始配置加载失败:\n" + configFile);
    }

    DownloadManager downloadManager;
    downloadManager.setServers(configManager.servers());
    downloadManager.setLocalBaseDir(configManager.localBaseDir());

    ProcessManager processManager;
    FileOperationManager fileManager;
    BookmarkManager bookmarkManager;
    InstallManager installManager;
    installManager.setDependencies(&configManager, &downloadManager,
                                   &processManager, &fileManager);

    TreeModel treeModel;
    treeModel.setConfigManager(&configManager);
    treeModel.reload();

    InstallItemModel itemModel;
    itemModel.setConfigManager(&configManager);
    itemModel.loadAll();

    // ---------- 5. 主窗口 ----------
    MainWindow w(&configManager, &downloadManager,
                 &bookmarkManager,
                 &installManager,
                 &treeModel, &itemModel);
    w.show();

    // ---------- 6. 退出时释放 TIHelper.dll ----------
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        TIHelperWrapper::instance().unload();
        Logger::instance().shutdown();   // ★ 关闭日志
    });

    return app.exec();
}
