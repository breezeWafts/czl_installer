# czl_installer

Windows 软件安装管理器：按类别展示可安装软件，在安装前清理环境、安装后执行
注册命令，并支持**两种配置来源**：

| 模式 | 配置来源 | 软件包存储 | 适用场景 |
| ---- | -------- | ---------- | -------- |
| **模式一** | JSON 配置文件（`config/config_ftp.json`） | FTP 服务器 | 离线/内网小规模，手工维护 JSON |
| **模式二** | PostgreSQL 数据库（`czl_*` 表） | HTTP 网络服务器路径 | 由 Web 管理端（`../../tornado/installer_back/`）统一维护，可多人协作、上传即发布 |

两种模式产出**完全相同的内部数据结构**，界面、类别树、安装流程、日志全部一致，
运行中可在顶部工具栏随时切换。

- Qt 版本：**6.12**（用 6.11.2 / MinGW 13.1 实测编译运行通过）
- 语言标准：C++17
- 构建方式：**qmake**（本工程 `.pro`，或上层 `../czl_suite.pro` 一键构建）
- 界面：Qt Widgets（`QTreeView` + 卡片式列表），**不使用 QML**
- 模式二数据库访问：**Qt 自带 `QSqlDatabase("QPSQL")`**，不引入任何第三方 C++ 库

---

## 0. 与 TIHelper 的关系（一个会话两个工程）

TIHelper 是**独立工程**，本程序通过 `TIHelper.dll` 调用它的
TrustedInstaller 提权能力。两者的协作已经全自动化：

```
czl_suite.pro            <- 顶层 subdirs（在 Qt Creator 里同时打开两个工程）
├── TIHelper/TIHelper.pro        构建出 TIHelper.dll
│       └─ QMAKE_POST_LINK ──┐   自动拷贝
└── czl_installer/czl_installer.pro
        └─ QMAKE_POST_LINK ──┘   再拷到 exe 输出目录
```

- `czl_installer.depends = TIHelper` 保证先出 DLL 再构建主程序；
- TIHelper 构建完成后打印
  `[TIHelper] copied TIHelper.dll to E:/codes/cpp/czl_installer`；
- 可用 `qmake "TIHELPER_CONSUMER_DIR=<路径>"` 改变拷贝目标。

**构建方式（三选一）**

```bat
:: 1) 一键构建两个工程（推荐）
qmake ..\czl_suite.pro -spec win32-g++
mingw32-make -j4

:: 2) 只构建本工程（需要 czl_installer\TIHelper.dll 已存在）
qmake czl_installer.pro -spec win32-g++
mingw32-make -j4

:: 3) Qt Creator：直接打开 ..\czl_suite.pro，项目树里会同时列出两个工程
```

---

## 1. 目录结构

```
czl_installer/
├── czl_installer.pro
├── main.cpp
├── MainWindow.h / .cpp
├── app.rc / app.manifest / app.ico / app.png / app.qrc
├── tihelper.h                 ← 由 TIHelper 工程同步的公开接口头
├── TIHelper.dll               ← 由 TIHelper 工程自动拷贝（构建产物）
├── core/
│   ├── ConfigManager.h / .cpp        配置解析 + 两种来源的切换与统一快照
│   ├── PgConfig.h / .cpp             ★ 模式二：连接串解析（URI/key=value/简写）
│   ├── DbSource.h / .cpp             ★ 模式二：读 czl_* 表 → 统一结构体
│   ├── DownloadManager.h / .cpp      服务器探测 + 主备切换（GUI 线程）
│   ├── InstallWorker.h / .cpp        后台线程执行安装（含续传与 SHA-256 校验）
│   ├── InstallManager.h / .cpp       任务调度 + 配置来源切换
│   ├── ProcessManager.h / .cpp       停服务 / 结束进程（原生 Win32）
│   ├── FileOperationManager.h / .cpp 复制 / 解压
│   ├── BookmarkManager.h / .cpp      浏览器书签写入
│   ├── Logger.h / .cpp               日志 + Qt 消息重定向
│   └── TIHelperWrapper.h / .cpp      TIHelper.dll 封装
├── models/
│   ├── TreeModel.h / .cpp            类别树
│   └── InstallItemModel.h / .cpp     安装项列表
├── ui/
│   └── Theme.h / .cpp                全局配色与样式表
├── tools/
│   └── make_icon.py                  生成 app.ico / app.png
├── config/
│   ├── config_ftp.json               ★ 模式一：JSON + FTP（默认）
│   ├── config_http.json              模式一的历史示例（HTTP 服务器备选）
│   └── config_pg.json                ★ 模式二：PostgreSQL 连接参数
├── deploy/
│   ├── deploy_to_dist.bat            ★ 一键生成 dist（含全部依赖 + QPSQL 驱动）
│   └── package.bat                   旧的 staging 脚本
└── dist/                             ★ 部署产物（含全部依赖，可直接运行）
```

> Web 管理端（软件包上传下载、分类/软件项/服务器/书签的可视化编辑）是**独立项目**，
> 位于 `../../tornado/installer_back/`（`tornado/` 是容器目录，pipenv 的 `.venv`
> 与 `Pipfile` 在那一层），用 Tornado + Jinja2 + psycopg 实现，建表脚本在
> `tornado/sql/schema.sql`。安装器的模式二直接读它写入的同一批表。

---

## 2. 应用图标

`app.ico` 由 `tools/make_icon.py` 生成（Pillow 绘制：深蓝圆角方块 +
白色安装箭头 + 托盘线），共 9 档尺寸：

```
16, 20, 24, 32, 40, 48, 64, 128, 256
```

重新生成：

```bat
python tools\make_icon.py
```

图标通过两条路径生效：

| 用途 | 机制 |
| ---- | ---- |
| exe 在资源管理器 / 任务栏的图标 | `app.rc` 里的 `IDI_ICON1 ICON "app.ico"`（编译进 `RT_GROUP_ICON`） |
| 窗口标题栏 / 任务栏分组图标 | `app.qrc` 内嵌 `app.png`，`MainWindow::applyWindowIcon()` 设置 |

`app.ico` 同时作为 `RT_ICON` 存在 9 个子资源，Windows 会按显示场景
自动挑选合适尺寸，不会出现小尺寸下模糊的问题。

---

## 3. 界面

配色与样式集中在 `ui/Theme.h` / `ui/Theme.cpp`，由
`main()` 里一次 `setStyleSheet(Theme::globalStyleSheet())` 全局生效，
取代旧版散落在各控件上的硬编码样式字符串。

- 顶部品牌栏：`czl_installer` 标识 + 服务器切换 + 右侧服务器状态胶囊
- 左侧类别树：圆角选中态、悬停高亮
- 右侧安装项卡片：名称 + 类型徽标（`EXE`/`MSI`/`BAT`/复制/复制+注册）+
  版本·发布者 + 描述 + 安装状态 + 进度条，右侧绿色「安装」按钮
  （失败后自动变橙色「重试」）
- 底部状态栏：进度文案、缓存目录、设置浏览器书签
- 统一滚动条、Tooltip、消息框样式

界面预览见 `ui-preview.png`。

---

## 4. 运行

```bat
:: ---------- 模式一：JSON 配置文件 + FTP 软件存储 ----------
czl_installer.exe                      :: 默认 config/config_ftp.json
czl_installer.exe --json               :: 同上（显式）
czl_installer.exe --ftp                :: 同上（兼容旧写法）
czl_installer.exe --config D:\my\cfg.json

:: ---------- 模式二：PostgreSQL 数据库 + HTTP 软件存储 ----------
czl_installer.exe --pg                 :: 读 config/config_pg.json 的连接参数
czl_installer.exe --pg-conn "192.168.1.1:5432,alice,alice123,czl_installer"
czl_installer.exe --pg-conn "postgresql://alice:alice123@192.168.1.1:5432/czl_installer"
czl_installer.exe --pg-conn "host=192.168.1.1 port=5432 user=alice password=alice123 dbname=czl_installer"

czl_installer.exe --help
```

> 相对路径配置按 **exe 所在目录**解析，因此从任何工作目录启动都能找到
> `config/`（旧版按当前工作目录解析，从别处启动必然失败）。

程序带 `requireAdministrator` 清单，启动时会请求 UAC 提权。

**运行时切换来源**：顶部工具栏的「JSON · FTP 存储 / PostgreSQL · HTTP 存储」
下拉框可直接切换（切换时会重新读配置；失败会弹提示并自动把下拉框拨回真实来源，
不会出现「界面显示 PG、数据其实还是 JSON」的状态）。旁边的「刷新配置」按钮用于
按当前来源重载一次 —— Web 管理端改完软件项后按它即可生效，不用重启安装器。

---

## 5. 生成 dist（含全部依赖）

一条命令把主程序 + TIHelper.dll + 全部 Qt6 运行时收集到 `dist\`：

```bat
set QT_DIR=D:\Qt\6.12.0\mingw_64
deploy\deploy_to_dist.bat
```

产出（约 37 MB、23 个文件）：

```
dist\
  czl_installer.exe
  TIHelper.dll
  Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll  Qt6Network.dll  Qt6Svg.dll  Qt6Sql.dll
  libgcc_s_seh-1.dll  libstdc++-6.dll  libwinpthread-1.dll
  platforms\qwindows.dll
  styles\  iconengines\  imageformats\  networkinformation\  tls\  generic\
  sqldrivers\qsqlpsql.dll      ← 模式二必需（Qt 的 PostgreSQL 驱动插件）
  libpq.dll  libssl-3-x64.dll  libcrypto-3-x64.dll  ← 模式二必需（来自 PostgreSQL 客户端）
  config\config_ftp.json  config_http.json  config_pg.json
```

脚本行为：

1. 定位工程根目录与编译输出；
2. **自动探测编译目录**：扫描同级 `build-*` 目录，按「由新到旧」逐个尝试
   三种布局（`<dir>\`、`<dir>\czl_installer\`、`<dir>\release\`），
   且 **Qt6 套件的目录优先**，只有找不到 Qt6 产物时才退回 Qt5 目录并打印告警
   —— 避免误把陈旧的 Qt5 exe 打进 dist。也可用 `BUILD_DIR` 显式指定；
3. 清空并重建 `dist`，拷入 exe 与 `TIHelper.dll`；
4. 调 `windeployqt`（`--no-quick-import`、`--compiler-runtime`，带 `--dir`）；
5. 兜底补齐 MinGW 运行库（`windeployqt` 偶尔漏 `libstdc++-6.dll`）；
6. 拷入 `config\`；
7. **部署模式二的数据库依赖**：从 `%QT_DIR%\plugins\sqldrivers\` 拷入
   `qsqlpsql.dll`，从 `%PGSQL_DIR%`（默认自动探测
   `C:\Program Files\PostgreSQL\{18,17,16}\bin`）拷入 `libpq.dll` 及其依赖；
8. 校验必需文件，缺失则非零退出；模式二的三个可选文件单独提示但不判失败。

可选环境变量：`QT_DIR`（必填）、`BUILD_DIR`、`MINGW_DIR`、`DIST_DIR`、
**`PGSQL_DIR`**。

> **模式二缺依赖的典型症状**：点「PostgreSQL」后弹
> 「Qt 未安装 QPSQL 数据库驱动」→ 说明 `sqldrivers\qsqlpsql.dll` 没进 dist；
> 弹 `QPSQL driver not loaded` 并带 `libpq` 字样 → 说明 `libpq.dll` 及其
> 依赖（`libssl-3-x64.dll` 等）没进 dist。
> 官方 Qt 安装器里 QPSQL 驱动属于
> **Qt 6.x.x → Additional Libraries → Qt PostgreSQL** 组件，默认不勾选。

> **批处理文件刻意保持纯 ASCII**：`cmd.exe` 按系统 ANSI 代码页（中文
> Windows 为 GBK/936）读取 `.bat`，UTF-8 中文注释会让 cmd 解析错乱。
> 后续修改这两个 `.bat` 时请勿加入中文。

---

## 6. 模式一：JSON 配置文件要点

```jsonc
{
  "localBaseDir": "D:/c_temp",       // 下载缓存与日志根目录
  "servers": [                        // 多地址备选，按顺序探测
    { "name": "ftp1-主用", "type": "ftp",
      "host": "192.168.1.100", "port": 21,
      "username": "anonymous", "password": "",
      "basePath": "/packages/", "connectTimeout": 5000 },
    { "name": "http2-备用", "type": "http",
      "baseUrl": "http://192.168.1.101:8080/packages/" }
  ],
  "bookmarks": [
    { "name": "公司官网", "url": "https://www.example.com" }
  ],
  "categories": [
    { "id": "cat_office", "name": "办公软件",
      "children": [
        { "id": "sub_wps", "name": "WPS 系列",
          "items": [
            { "id": "item_wps_office",       // 实际 id 为 cat_office/sub_wps/item_wps_office
              "name": "WPS Office 2023",
              "type": "msi",                 // msi | exe | bat | copy_only | copy_and_register
              "remotePath": "office/wps_office_2023.msi",
              "localTempPath": "office/wps_office_2023.msi",
              "installArgs": ["/quiet"],     // 推荐数组写法
              "killServices": ["WPSUpdateService"],
              "killProcesses": ["wps.exe"] }
          ] }
      ] }
  ]
}
```

### `installArgs` 的两种写法

```jsonc
"installArgs": ["/configure", "config.xml"]   // 推荐：数组，逐项精确传入
"installArgs": "/configure config.xml"        // 字符串：按「引号外空白」切分
```

> 旧版对字符串形式用 `QString::split(' ')` 硬切，`"/configure config.xml"`
> 会变成两个参数并被重新拼错；现在两种写法都会正确处理引号。

### 安装项的 id

子类别下的安装项，其 id 会被规范化为 `父类别/子类别/项`（例如
`cat_office/sub_wps/item_wps_office`）。因此**点击父类别也能列出全部
后代安装项**，而旧版只列直接 `items`，选父节点常常是空列表。

---

## 7. 模式二：PostgreSQL 配置 + HTTP 软件存储

### 7.1 连接参数

优先级：**命令行 `--pg-conn` > `config/config_pg.json` > 内置默认值**。

内置默认值（与需求一致）：

```
host=192.168.1.1  port=5432  user=alice  password=alice123  dbname=czl_installer
```

`config/config_pg.json` 支持两种写法，**分字段写法优先**：

```jsonc
{
  // 写法 A：分字段（推荐，便于运维按行核对）
  "host": "192.168.1.1", "port": 5432,
  "database": "czl_installer", "user": "alice", "password": "alice123",
  "connectTimeout": 8,

  // 写法 B：整串（写法 A 存在时本项被忽略）
  "connectionString": "192.168.1.1:5432,alice,alice123,czl_installer"
}
```

`--pg-conn` 与 `connectionString` 都支持四种语法（由 `core/PgConfig.cpp` 解析）：

| 形式 | 示例 |
| ---- | ---- |
| libpq URI | `postgresql://alice:alice123@192.168.1.1:5432/czl_installer?connect_timeout=8` |
| key=value | `host=192.168.1.1 port=5432 user=alice password=alice123 dbname=czl_installer` |
| 逗号简写 | `192.168.1.1:5432,alice,alice123,czl_installer` |
| 纯主机 | `192.168.1.1`（其余字段用默认值） |

### 7.2 数据库表 → 安装器字段映射

数据库由 Web 管理端（`../../tornado/installer_back/`）维护，建表脚本为
`tornado/sql/schema.sql`。安装器**只读**这些表：

| 表 | 安装器读取的列 | 对应 JSON 字段 | 说明 |
| -- | -------------- | -------------- | ---- |
| `czl_settings` | `localBaseDir` | `localBaseDir` | 本地缓存/日志根目录 |
| `czl_settings` | `installerHttpBaseUrl` | —（模式二新增） | 安装器读取的 HTTP 下载根地址，仅作展示/自检参考；真正生效的下载地址来自 `czl_servers.base_url` |
| `czl_settings` | `downloadRoot` | — | nginx 暴露的磁盘目录，仅供运维核对 |
| `czl_servers` | `name / type / host / port / base_path / base_url / username / password / connect_timeout` | `servers[]` | 只取 `enabled = true`，按 `priority, id` 升序，与 JSON 的数组顺序等价（**主备顺序**） |
| `czl_categories` | `code / name / parent_id / sort_order` | `categories[].id / name / children[]` | 由 `parent_id` 还原成 JSON 的嵌套树；`sort_order, id` 决定同级顺序 |
| `czl_items` | `code / name / type / description / version / publisher / remote_path / local_temp_path / install_args / target_path / overwrite / files / register_commands / kill_processes / kill_services / file_size / sha256` | 安装项各字段 | 只取 `enabled = true` |
| `czl_item_sources` | `kind / remote_path / local_path / files / target_path` | `sources[]` | 按 `item_id, sort_order, id` 排序 |
| `czl_bookmarks` | `name / url` | `bookmarks[]` | 只取 `enabled = true`，按 `sort_order, id` 排序 |

映射规则细节（保证两种模式行为一致）：

1. **`items[].id`**：数据库里 `czl_items.code` 与 JSON 里的 `id` 同义
   （例如 `item_wps_office`）。装进类别树时同样会被规范化为
   `分类code/父分类code/项`，所以「点父类别列出全部后代」在两种模式下都成立。
2. **`installArgs`**：jsonb 列。数组按项使用；字符串按「引号外空白」切分
   —— 与 JSON 的字符串写法完全同一套规则（`DbSource::parseArgsValue`）。
3. **`files / register_commands / kill_processes / kill_services`**：jsonb 数组；
   若运维写成纯文本（多行），会按行拆分兜底。
4. **`sources[].kind`**：留空时按 `remote_path` 推断 —— 以 `/` 或 `\` 结尾是
   `dir`，以 `.zip` 结尾是 `zip`，其余是 `file`。
5. **`sha256 / file_size`**：软件项没填就自动回退到 `czl_packages` 里同
   `rel_path` 的登记值。安装器下载完主程序包后会**强制校验**，不一致就删除
   并换下一个备选服务器重下（见 8.2）。

### 7.3 数据一致性兜底

数据库是人工维护的，容易留下脏数据，安装器的处理策略是**宁可降级也不要崩**：

| 情况 | 处理 |
| ---- | ---- |
| `czl_servers` 一条可用记录都没有 | 加载失败，保留上一份可用配置，界面弹中文错误 |
| 软件项引用了不存在的分类 | 归入自动创建的「未分类」，并记一条 warning |
| 分类的 `parent_id` 指向不存在的记录或成环 | 该分类提升为顶层，并记 warning |
| 某条 `czl_item_sources` 找不到对应的启用软件项 | 忽略该源并记 warning |
| `czl_bookmarks.url` 为空 | 丢弃（与 JSON 模式一致） |
| 数据库整体不可达 | 保留上一次成功加载的数据；界面顶部下拉框会拨回真实来源 |

---

## 8. 各类型安装项的行为（两种模式通用）

| type | 行为 |
| ---- | ---- |
| `msi` | 下载后拼 `msiexec /i <file> <installArgs> /qn /norestart` 执行 |
| `exe` | 下载后以 TI 权限带参数执行 |
| `bat` | 下载后以 TI 权限执行 |
| `copy_only` | 按 `sources[]` 下载并复制 / 解压到 `targetPath` |
| `copy_and_register` | 同上，最后以 TI 权限执行 `registerCommands[]` |

`sources[].kind` 取值：`file`（单文件）、`dir`（需配 `files[]` 清单）、
`zip`（解压到 `targetPath`）。

> `msi` 只有在配置里**完全没有** `/q*`、`/quiet`、`/passive` 时才会补
> `/qn`，避免出现两个互斥的 UI 参数（旧版无条件追加 `/qn`）。

### 8.1 断点续传

下载先落到 `<目标>.part`，成功后再改名，避免半截文件被当成成品。
现在**不再无脑删除 `.part`**：下次下载会按已有字节数发
`Range: bytes=N-` 续传（模式二的 HTTP/nginx 支持良好）。
若服务器返回 `200` 而不是 `206`（不支持 Range，或 FTP），会**截断文件重新写**，
因此续传逻辑不会产生「旧片段 + 整包」的坏文件。

### 8.2 SHA-256 校验

模式二下 `czl_items.sha256`（或 `czl_packages.sha256`）有值时，主程序包下载完成
后会计算 SHA-256 并比对：

- 先比 `file_size`（不一致直接判失败，省掉几个 GB 的哈希计算）；
- 再分块（1 MiB）计算 SHA-256；
- 不一致 → 删除坏包 → 自动换下一个备选服务器重下；
- 模式一的 JSON 没有该字段，校验自动跳过。

> 目录型安装项（`files[]` 逐文件下载）与 `copy_*` 的源文件不做逐文件校验，
> 因为校验和对应的是整个包。

---

## 9. 已修正的问题

### 9.1 编译与工程

| 问题 | 修正 |
| ---- | ---- |
| 缺少 `<functional>` 却使用 `std::function`，部分标准库下直接编译失败 | `ConfigManager` 补头文件；`TreeModel` 改用显式栈 |
| `QTextStream::setCodec` 在 Qt6 已删除 | 改用 `setEncoding(QStringConverter::Utf8)` / `toLocal8Bit()` |
| `TreeModel` 构造函数把根节点留为 `nullptr`，视图在 `setCategories()` 前访问即崩溃 | 构造时即建立根节点 |
| 源码含中文却不指定源码字符集，MSVC 会按 ANSI 读成乱码 | `.pro` 加 `/utf-8`（MSVC）与 `-finput-charset=UTF-8 -fexec-charset=UTF-8`（MinGW） |
| 含中文的 `const char*` 用 `QString::fromLatin1` 转换 | 改用 `QString::fromUtf8`（否则显示为 `å¤å¶`） |
| `QT += widgets network core gui` 含从未使用的 qml/quick | 只保留 `core gui widgets network` |
| `debug_and_release` 双构建导致目标名冲突 | 关闭 |
| HTTP 配置里是 `"... 与 xxx 相同 ..."` 非法 JSON 占位符 | 换成完整可用的配置 |

### 9.2 架构与线程

**旧版 `InstallManager::installItem()` 在 GUI 线程里同步跑完
「探测 → 下载 → 执行安装」，靠 `QCoreApplication::processEvents()`
刷新进度：界面卡死，且 `processEvents()` 会重入任意槽函数 ——
用户能在安装进行中再次点击安装。**

现在拆分为：

- `DownloadManager`：只在 GUI 线程做**探测**与主备切换；
- **`InstallWorker`**：在独立 `QThread` 上完成全流程，通过**排队信号**
  把进度送回 GUI；
- 全工程**不再有任何 `processEvents()` 调用**；
- `installItem()` 增加 `isBusy()` 保护，安装期间重复点击被明确拒绝。

### 9.3 功能性 Bug

| 问题 | 后果 / 修正 |
| ---- | ---- |
| 类别筛选用错角色（`IdRole` 被定义成 `Qt::DisplayRole` 的别名） | 拿到的是显示名而不是 id，右侧列表**恒为空**；改用 `TreeModel::IdRole`，并让启动时自动选中第一个类别 |
| 每次进度回调都重建整份列表 | N 个卡片控件被反复销毁重建；改为原地更新单个条目 + `setUpdatesEnabled(false)` |
| 安装状态存在行结构体里，`loadAll()` 一律重置 | 切换类别后状态丢失；状态移入独立 `QHash` 持久保存 |
| 生成的 bat 用 `setCodec("UTF-8")` 写盘 | `cmd.exe` 按 ANSI 读取，中文/含空格路径**必然乱码**；改用 `toLocal8Bit()` + 脚本内 `chcp` |
| `--http` / `--ftp` 只改了局部变量 | `DownloadManager` 拿到的是另一份配置，**切换实际未生效**；配置还被加载两次。改用 `QCommandLineParser`，只加载一次 |
| 服务器探测错误分类写反 | 4xx/5xx 与认证失败被判为「可达」；改为显式白名单，探测与下载共用 |
| 服务/进程状态靠解析 `sc`/`tasklist`/`wmic` 的本地化文本 | 非英文系统判断不可靠，`wmic` 已被弃用；全改原生 Win32（`QueryServiceStatusEx`/`ControlService`/`TerminateProcess`） |
| ZIP 解压用「条目数连续 3 次不变」猜结束 | 大文件会提前返回；优先用 Win10+ 自带 `tar.exe`，COM 回退改为按字节数稳定判断 |
| `QFile::copy` 在跨卷/被占用/只读时直接失败 | 改原生分块复制，清只读属性、保留时间戳、失败重试 3 次 |
| 下载直接写目标文件 | 失败留下半截文件；改为先写 `.part` 再改名 |
| 浏览器书签只扫 `Default` 配置目录 | 多配置文件（`Profile 1/2…`）被漏掉 |
| 书签备份文件名固定 | 第二次运行覆盖上次备份；改为带时间戳 |
| `taskkill` 后 `msleep(1200)` 靠猜 | 书签文件可能仍被占用；改为轮询确认进程退出 |
| `applyToAll()` 全部失败时仍可能报成功 | `ok = 至少一个浏览器写入成功` |
| `main.cpp` 在栈上创建全部管理器 | 统一挂到 `QApplication` 上，生命周期清晰 |

### 9.4 TIHelper 侧

TIHelper 的修正见 `../TIHelper/README.MD`，其中最严重的是
**manifest 命名空间拼写错误（`asm vener3`）导致 UAC 提权声明整段失效**。

### 9.5 本次升级（3.0：模式二 + Web 管理端）

| 变更 | 说明 |
| ---- | ---- |
| `.pro` 增加 `QT += sql`，新增 `core/PgConfig.*` 与 `core/DbSource.*` | 用 Qt 自带 QPSQL 驱动读 PostgreSQL，不引入第三方 C++ 库 |
| `ConfigManager` 重构为双来源 | 新增 `ConfigSource` 枚举、`loadFromPostgres()`、`setSource()`；两种来源提交同一批结构体，`TreeModel` / `InstallManager` / `MainWindow` 无需区分 |
| `InstallItem` 增加 `fileSize` / `sha256` | 支撑下载后的完整性校验；JSON 模式自然为空 |
| CLI 重构 | `--http` 被 `--pg` 取代（`--ftp`/`--json` 兼容保留），新增 `--pg-conn`；启动只加载一次配置 |
| 顶部工具栏 | 下拉框改为「JSON · FTP 存储 / PostgreSQL · HTTP 存储」，新增「刷新配置」按钮；切换失败自动回滚下拉框 |
| 下载增强 | 断点续传（Range）+ SHA-256 强制校验 + 大小预检 |
| 部署脚本 | 自动部署 `sqldrivers\qsqlpsql.dll` 与 `libpq.dll` 等，并区分「必需缺失」与「模式二可选缺失」 |

---

## 10. 日志

日志目录为 `<localBaseDir>/log`，文件名 `czl_installer_yyyyMMdd.log`，
内容为 UTF-8。`qDebug` / `qInfo` / `qWarning` / `qCritical` 会被
`Logger::messageHandler` 自动重定向落盘；Debug 构建下同时输出到 stderr。

> 消息处理器内部刻意使用 `fprintf` 而非 `qDebug()` ——
> 在消息处理器里再调用 `qDebug` 会造成无限递归。


---
