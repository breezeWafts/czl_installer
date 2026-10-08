# ============================================================================
# czl_installer —— 软件安装管理器（Qt 6.12 / C++17）
#
# 相对 Qt5 旧工程的主要变化：
#   * QT 模块只保留真正需要的 core / gui / widgets / network
#     （去掉旧工程里已无用的 qml / quick 依赖；gui 是 Qt Widgets 的必需依赖）；
#   * 使用 Qt6 的 QTextStream::setEncoding / QStringConverter 取代已删除的 setCodec；
#   * _WIN32_WINNT 提升到 0x0601（Win7）并补全 Win32 依赖库；
#   * 关闭 debug_and_release 双构建，避免 Qt6 下既生成 debug 又生成 release 目标名冲突。
# ============================================================================

QT += core gui widgets network

CONFIG += c++17
CONFIG -= debug_and_release debug_and_release_target

# 打开 Qt 6 中被弃用 API 的编译期告警，便于后续继续清理。
#
# ★ /utf-8 很关键：源码里含中文（界面文字与注释），且保存为不带 BOM 的
#   UTF-8。MSVC 默认按系统 ANSI 代码页读取源文件，会把中文字面量编译成
#   乱码，必须显式指定源码与执行字符集都是 UTF-8。
#   MinGW 默认即 UTF-8，无需额外开关。
win32-msvc*: QMAKE_CXXFLAGS += /utf-8 /permissive- /W4
win32-g++:   QMAKE_CXXFLAGS += -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8

TARGET   = czl_installer
TEMPLATE = app

INCLUDEPATH += $$PWD $$PWD/core $$PWD/models $$PWD/ui

# 窗口/任务栏图标（PNG 内嵌，避免运行时依赖外部图标文件）
RESOURCES += app.qrc

win32 {
    DEFINES += _WIN32_WINNT=0x0601 WINVER=0x0601 UNICODE _UNICODE

    # advapi32：服务控制（OpenSCManager/StartService）
    # user32  ：窗口与消息
    # shell32 ：Shell API / 路径工具
    # ole32   ：COM（ZIP 解压走 Shell.Application）
    # wtsapi32：会话相关 API
    LIBS += -lAdvapi32 -lUser32 -lShell32 -lOle32 -lOleAut32 -lUuid -lWtsApi32

    # ★ UAC manifest + 应用图标（MSVC 与 MinGW 通用）
    #   app.rc 内同时包含 RT_MANIFEST 与 RT_GROUP_ICON，
    #   因此 exe 在资源管理器和任务栏都会显示 app.ico。
    RC_FILE = app.rc

    # ---------- MSVC ----------
    msvc {
        QMAKE_LFLAGS += /SUBSYSTEM:WINDOWS,6.01
    }

    # ---------- MinGW ----------
    gcc | mingw {
        # Windows GUI 子系统（不弹控制台）
        QMAKE_LFLAGS += -Wl,--subsystem,windows
        QMAKE_LFLAGS += -Wl,--major-subsystem-version,6
        QMAKE_LFLAGS += -Wl,--minor-subsystem-version,1
    }

    # ---------- 编译后自动拷贝 TIHelper.dll 与 config ----------
    # TIHelper.dll 由 TIHelper 工程的 QMAKE_POST_LINK 自动放到 $$PWD，
    # 因此这里通常能直接拷到；若手工删过，先重新构建 TIHelper 即可。
    TIHELPER_SRC = $$shell_path($$PWD/TIHelper.dll)
    TIHELPER_DST = $$shell_path($$OUT_PWD/TIHelper.dll)
    QMAKE_POST_LINK += cmd /c if not exist \"$$TIHELPER_SRC\" echo [czl_installer] WARNING: TIHelper.dll not found, build TIHelper first
    QMAKE_POST_LINK += $$escape_expand(\\n\\t)
    QMAKE_POST_LINK += cmd /c if exist \"$$TIHELPER_SRC\" copy /Y \"$$TIHELPER_SRC\" \"$$TIHELPER_DST\" >nul
    QMAKE_POST_LINK += $$escape_expand(\\n\\t)

    CONFIG_SRC = $$shell_path($$PWD/config)
    CONFIG_DST = $$shell_path($$OUT_PWD/config)
    QMAKE_POST_LINK += cmd /c if exist \"$$CONFIG_SRC\" xcopy /Y /I /E \"$$CONFIG_SRC\" \"$$CONFIG_DST\" >nul
}

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    core/BookmarkManager.cpp \
    core/ConfigManager.cpp \
    core/DownloadManager.cpp \
    core/FileOperationManager.cpp \
    core/InstallManager.cpp \
    core/InstallWorker.cpp \
    core/Logger.cpp \
    core/ProcessManager.cpp \
    core/TIHelperWrapper.cpp \
    models/InstallItemModel.cpp \
    models/TreeModel.cpp \
    ui/Theme.cpp

HEADERS += \
    MainWindow.h \
    tihelper.h \
    core/BookmarkManager.h \
    core/ConfigManager.h \
    core/DownloadManager.h \
    core/FileOperationManager.h \
    core/InstallManager.h \
    core/InstallWorker.h \
    core/Logger.h \
    core/ProcessManager.h \
    core/TIHelperWrapper.h \
    models/InstallItemModel.h \
    models/TreeModel.h \
    ui/Theme.h

DISTFILES += \
    README.MD \
    app.manifest \
    app.rc \
    app.ico \
    app.qrc \
    config/config_ftp.json \
    config/config_http.json \
    deploy/package.bat \
    deploy/deploy_to_dist.bat \
    tools/make_icon.py
