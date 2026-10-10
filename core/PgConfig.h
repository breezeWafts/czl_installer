#ifndef PGCONFIG_H
#define PGCONFIG_H

#include <QString>

// ---------------------------------------------------------------------------
// PostgreSQL 连接参数（纯数据，可自由拷贝）
// ---------------------------------------------------------------------------

/**
 * @brief 模式二（PG 配置 + HTTP 软件存储）的数据库连接参数。
 *
 * 之所以不直接用裸连接串：配置来自多个地方（config/config_pg.json、
 * 命令行 --pg-conn、环境变量），需要统一解析、校验与展示。
 * 最终仍会拼成 libpq 的 key=value 连接串交给 QPSQL 驱动。
 */
struct PgConnectionInfo {
    QString host = QStringLiteral("192.168.1.1");
    int     port = 5432;
    QString database = QStringLiteral("czl_installer");
    QString user = QStringLiteral("alice");
    QString password = QStringLiteral("alice123");
    int     connectTimeout = 8;      ///< 秒（libpq connect_timeout）

    /// 拼成 libpq key=value 连接串（值统一单引号包裹并转义）。
    QString toConnectionString() const;

    /// 不含密码的展示串，用于日志与界面提示。
    QString describe() const;
};

/**
 * @brief 解析连接串，支持四种写法（按顺序尝试）：
 *
 *   1. URI      : postgresql://alice:alice123@192.168.1.1:5432/czl_installer?connect_timeout=8
 *   2. key=value: host=192.168.1.1 port=5432 user=alice password=alice123 dbname=czl_installer
 *   3. 简写     : 192.168.1.1:5432,alice,alice123[,czl_installer]
 *   4. 只有主机 : 192.168.1.1
 *
 * 解析成功返回 true（未给出的字段保留 info 中的原值）。
 */
bool parsePgConnectionString(const QString &text, PgConnectionInfo &info);

/// 生成默认连接串（与 PgConnectionInfo 的默认值一致）。
QString defaultPgConnectionString();

#endif // PGCONFIG_H
