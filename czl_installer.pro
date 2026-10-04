QT += widgets network core gui

CONFIG += c++17
CONFIG -= debug_and_release

TARGET = czl_installer
TEMPLATE = app

INCLUDEPATH += $$PWD/core $$PWD/models

win32 {
    DEFINES += _WIN32_WINNT=0x0601 WINVER=0x0601
    LIBS += -lAdvapi32 -lUser32 -lShell32 -lWtsApi32 -lOle32 -lOleAut32 -luuid

    # ★ 加入 manifest 资源（两个编译器通用）
    RC_FILE = app.rc

    # ---------- MSVC ----------
    msvc {
        QMAKE_LFLAGS += /SUBSYSTEM:WINDOWS,6.01
        QMAKE_LFLAGS += /MANIFESTUAC:\"level=\'requireAdministrator\' uiAccess=\'false\'\"
    }

    # ---------- MinGW ----------
    gcc | mingw {
        # 使用 Windows GUI 子系统（不弹控制台）
        QMAKE_LFLAGS += -Wl,--subsystem,windows

        # 关键：让 ld 从 .rc 生成的 .o 里自动找 manifest，
        # 不需要再额外指定 subsystem 版本号
        QMAKE_LFLAGS += -Wl,--major-subsystem-version,6
        QMAKE_LFLAGS += -Wl,--minor-subsystem-version,1
    }
    # ---------- 编译后自动拷贝 TIHelper.dll ----------
    TIHELPER_SRC = $$shell_path($$PWD/TIHelper.dll)
    TIHELPER_DST = $$shell_path($$OUT_PWD/TIHelper.dll)
    QMAKE_POST_LINK += cmd /c if exist \"$$TIHELPER_SRC\" copy /Y \"$$TIHELPER_SRC\" \"$$TIHELPER_DST\"
    CONFIG_SRC = $$shell_path($$PWD/config)
    CONFIG_DST = $$shell_path($$OUT_PWD/config)
    QMAKE_POST_LINK += cmd /c if exist \"$$CONFIG_SRC\" xcopy /Y /I /E \"$$CONFIG_SRC\" \"$$CONFIG_DST\"
}

SOURCES += \
    core/BookmarkManager.cpp \
    core/Logger.cpp \
    main.cpp \
    MainWindow.cpp \
    core/ConfigManager.cpp \
    core/DownloadManager.cpp \
    core/ProcessManager.cpp \
    core/FileOperationManager.cpp \
    core/TIHelperWrapper.cpp \
    core/InstallManager.cpp \
    models/TreeModel.cpp \
    models/InstallItemModel.cpp

HEADERS += \
    MainWindow.h \
    core/BookmarkManager.h \
    core/ConfigManager.h \
    core/DownloadManager.h \
    core/Logger.h \
    core/ProcessManager.h \
    core/FileOperationManager.h \
    core/TIHelperWrapper.h \
    core/InstallManager.h \
    models/TreeModel.h \
    models/InstallItemModel.h

# RESOURCES += qml.qrc


DISTFILES += \
    README.MD \
    app.manifest \
    app.rc \
    config/config_ftp.json \
    config/config_http.json \
    deploy/package.bat
