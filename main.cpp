#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QString>

#include "BookmarkManager.h"
#include "ConfigManager.h"
#include "DownloadManager.h"
#include "InstallManager.h"
#include "InstallItemModel.h"
#include "Logger.h"
#include "MainWindow.h"
#include "TIHelperWrapper.h"
#include "Theme.h"
#include "TreeModel.h"

namespace {

/// 解析命令行：--ftp / --http / --config <路径>
struct StartupOptions {
    QString configFile = QStringLiteral("config/config_ftp.json");
    QString serverType = QStringLiteral("ftp");
};

StartupOptions parseCommandLine(QApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("czl_installer —— 软件安装管理器"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption httpOption(
        QStringList{QStringLiteral("http")},
        QStringLiteral("使用 config/config_http.json 作为配置来源"));
    const QCommandLineOption ftpOption(
        QStringList{QStringLiteral("ftp")},
        QStringLiteral("使用 config/config_ftp.json 作为配置来源（默认）"));
    const QCommandLineOption configOption(
        QStringList{QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("显式指定配置文件路径"),
        QStringLiteral("path"));

    parser.addOption(httpOption);
    parser.addOption(ftpOption);
    parser.addOption(configOption);
    parser.process(app);

    StartupOptions options;

    // 显式 --config 优先级最高
    if (parser.isSet(configOption)) {
        options.configFile = parser.value(configOption);
        options.serverType = options.configFile.contains(QLatin1String("http"),
                                                         Qt::CaseInsensitive)
                                 ? QStringLiteral("http")
                                 : QStringLiteral("ftp");
        return options;
    }

    if (parser.isSet(httpOption)) {
        options.configFile = QStringLiteral("config/config_http.json");
        options.serverType = QStringLiteral("http");
    } else if (parser.isSet(ftpOption)) {
        options.configFile = QStringLiteral("config/config_ftp.json");
        options.serverType = QStringLiteral("ftp");
    }

    return options;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Qt 6.12：QApplication 的元数据应通过这几个 setter 设置。
    QCoreApplication::setApplicationName(QStringLiteral("czl_installer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("CZL"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("czl.local"));

    const StartupOptions options = parseCommandLine(app);

    // ---------- 0. 全局外观 ----------
    //   字体平滑 + 统一浅色主题样式表（取代旧版散落在各控件上的
    //   setStyleSheet 硬编码颜色）。
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, true);
    app.setStyleSheet(Theme::globalStyleSheet());

    // ---------- 1. 配置（只加载一次） ----------
    //   旧版在这里加载一次、随后又调用 configManager.loadConfig() 一次，
    //   第二次的返回值还被忽略了；更严重的是 --http 分支解析出的配置
    //   从未同步给 DownloadManager，树和下载用的是两套不同的服务器。
    ConfigManager configManager;

    const bool loaded = configManager.loadConfig(options.configFile);

    // ---------- 2. 日志系统（依赖 localBaseDir） ----------
    Logger::instance().init(configManager.localBaseDir());

    if (!loaded) {
        const QString detail = QStringLiteral("初始配置加载失败:\n%1\n\n%2")
                                   .arg(ConfigManager::resolvePath(options.configFile),
                                        configManager.lastError());
        Logger::error(detail);
        QMessageBox::warning(nullptr, QStringLiteral("配置错误"), detail);
    }

    Logger::info(QStringLiteral("配置文件: %1").arg(configManager.configFilePath()));
    Logger::info(QStringLiteral("本地缓存目录: %1").arg(configManager.localBaseDir()));
    Logger::info(QStringLiteral("启动参数: --%1").arg(options.serverType));

    // ---------- 3. TIHelper.dll ----------
    if (!TIHelperWrapper::instance().load()) {
        Logger::warn(QStringLiteral("TIHelper.dll 加载失败，"
                                    "TrustedInstaller 提权不可用: %1")
                         .arg(TIHelperWrapper::instance().lastError()));
    }

    // ---------- 4. 各管理器 ----------
    //   统一挂到 app 上，避免旧版那种「栈上对象 + 手动生命周期」的隐患。
    auto *downloadManager = new DownloadManager(&app);
    downloadManager->setServers(configManager.servers());

    auto *bookmarkManager = new BookmarkManager(&app);
    auto *installManager  = new InstallManager(&app);

    installManager->setDependencies(&configManager, downloadManager);

    // ProcessManager / FileOperationManager 由 InstallWorker 在后台线程内
    // 按需创建实例（它们是无状态的工具类），因此这里不再实例化。

    auto *treeModel = new TreeModel(&app);
    treeModel->setConfigManager(&configManager);
    treeModel->reload();

    auto *itemModel = new InstallItemModel(&app);
    itemModel->setConfigManager(&configManager);

    // ---------- 5. 主窗口 ----------
    MainWindow window(&configManager, downloadManager, bookmarkManager,
                      installManager, treeModel, itemModel);
    window.show();

    // ---------- 6. 退出清理 ----------
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        TIHelperWrapper::instance().unload();
        Logger::instance().shutdown();
    });

    return app.exec();
}
