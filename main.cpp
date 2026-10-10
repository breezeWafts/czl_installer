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
#include "PgConfig.h"
#include "TIHelperWrapper.h"
#include "Theme.h"
#include "TreeModel.h"

namespace {

/// 解析命令行：--json / --pg / --config <路径> / --pg-conn <连接串>
struct StartupOptions {
    ConfigManager::ConfigSource source = ConfigManager::ConfigSource::JsonFile;
    QString jsonFile = ConfigManager::defaultJsonConfigPath();
    QString pgConnection;             ///< 为空表示用 config/config_pg.json / 内置默认
};

StartupOptions parseCommandLine(QApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("czl_installer —— 软件安装管理器\n"
                       "模式一：JSON 配置文件 + FTP 软件存储\n"
                       "模式二：PostgreSQL 数据库 + HTTP 软件存储（由 Web 管理端维护）"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption jsonOption(
        QStringList{QStringLiteral("json"), QStringLiteral("ftp")},
        QStringLiteral("模式一：使用 JSON 配置文件（默认 config/config_ftp.json）"));
    const QCommandLineOption pgOption(
        QStringList{QStringLiteral("pg"), QStringLiteral("postgresql")},
        QStringLiteral("模式二：使用 PostgreSQL 数据库 + HTTP 下载"));
    const QCommandLineOption configOption(
        QStringList{QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("显式指定 JSON 配置文件路径（隐含模式一）"),
        QStringLiteral("path"));
    const QCommandLineOption pgConnOption(
        QStringList{QStringLiteral("pg-conn")},
        QStringLiteral("PostgreSQL 连接串，支持四种写法：\n"
                       "  postgresql://alice:alice123@192.168.1.1:5432/czl_installer\n"
                       "  host=192.168.1.1 port=5432 user=alice password=alice123 dbname=czl_installer\n"
                       "  192.168.1.1:5432,alice,alice123,czl_installer\n"
                       "  192.168.1.1"),
        QStringLiteral("conn"));

    parser.addOption(jsonOption);
    parser.addOption(pgOption);
    parser.addOption(configOption);
    parser.addOption(pgConnOption);
    parser.process(app);

    StartupOptions options;

    if (parser.isSet(pgOption)) {
        options.source = ConfigManager::ConfigSource::PostgreSQL;
    } else if (parser.isSet(jsonOption)) {
        options.source = ConfigManager::ConfigSource::JsonFile;
    }

    // 显式 --config 优先级高于 --pg：给了 JSON 文件就按模式一跑
    if (parser.isSet(configOption)) {
        options.jsonFile = parser.value(configOption);
        options.source = ConfigManager::ConfigSource::JsonFile;
    }

    if (parser.isSet(pgConnOption)) {
        options.pgConnection = parser.value(pgConnOption);
        options.source = ConfigManager::ConfigSource::PostgreSQL;
    }

    return options;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Qt 6.12：QApplication 的元数据应通过这几个 setter 设置。
    QCoreApplication::setApplicationName(QStringLiteral("czl_installer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("3.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("CZL"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("czl.local"));

    const StartupOptions options = parseCommandLine(app);

    // ---------- 0. 全局外观 ----------
    //   字体平滑 + 统一浅色主题样式表（取代旧版散落在各控件上的
    //   setStyleSheet 硬编码颜色）。
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setEffectEnabled(Qt::UI_AnimateCombo, true);
    app.setStyleSheet(Theme::globalStyleSheet());

    // ---------- 1. 配置 ----------
    //   两种来源产出的数据结构完全一致，后续所有组件都不需要区分。
    //   ConfigManager::setSource() 内部已实现优先级：
    //     命令行连接串 > config/config_pg.json > 内置默认（192.168.1.1:5432）
    ConfigManager configManager;

    const bool loaded =
        configManager.setSource(options.source, options.pgConnection);

    // ---------- 2. 日志系统（依赖 localBaseDir） ----------
    Logger::instance().init(configManager.localBaseDir());

    if (!loaded) {
        const QString origin =
            (options.source == ConfigManager::ConfigSource::PostgreSQL)
                ? QStringLiteral("PostgreSQL（--pg-conn 或 config/config_pg.json）")
                : ConfigManager::resolvePath(options.jsonFile);

        const QString detail = QStringLiteral("初始配置加载失败:\n%1\n\n%2")
                                   .arg(origin, configManager.lastError());
        Logger::error(detail);
        QMessageBox::warning(nullptr, QStringLiteral("配置错误"), detail);
    }

    Logger::info(QStringLiteral("配置来源: %1").arg(
        configManager.currentSource() == ConfigManager::ConfigSource::PostgreSQL
            ? QStringLiteral("PostgreSQL 数据库 + HTTP 下载")
            : QStringLiteral("JSON 配置文件 + FTP 下载")));
    Logger::info(QStringLiteral("配置位置: %1").arg(configManager.configFilePath()));
    Logger::info(QStringLiteral("本地缓存目录: %1").arg(configManager.localBaseDir()));
    for (const QString &warning : configManager.warnings())
        Logger::warn(warning);

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
