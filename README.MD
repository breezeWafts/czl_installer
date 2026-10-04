# 项目结构
czl_installer/
├── czl_installer.pro
├── main.cpp
├── main.qml
├── app.rc
├── app.manifest
├── qml.qrc
├── TIHelper.dll (外部提供)
├── ConfigManager.h / .cpp
├── DownloadManager.h / .cpp
├── InstallManager.h / .cpp
├── ProcessManager.h / .cpp
├── FileOperationManager.h / .cpp
├── TIHelperWrapper.h / .cpp
├── TreeModel.h / .cpp
├── InstallItemModel.h / .cpp
├── config/
│   ├── config_ftp.json
│   └── config_http.json
└── deploy/
    └── package.bat

# 部署目录
czl_installer/
├── czl_installer.exe                ← 主程序
├── TIHelper.dll                     ← 必须与 exe 同目录
├── config/
│   ├── config_ftp.json
│   └── config_http.json
├── Qt5Core.dll
├── Qt5Gui.dll
├── Qt5Qml.dll
├── Qt5Quick.dll
├── Qt5Network.dll
├── platforms/qwindows.dll
├── qml/QtQuick/...
├── vcruntime140.dll
├── msvcp140.dll
└── ucrtbase.dll                     ← Win7 需要

# 功能
能力	                                  实现位置
树形类别菜单（任意层级）	            models/TreeModel.* + main.qml
列表 + 安装按钮 + 悬停详情	            models/InstallItemModel.* + main.qml
FTP/HTTP 多地址备选	            core/DownloadManager.*
全不可达时中止并提示	            InstallManager::installItem 第①步
本地缓存 D:/c_temp（自动创建/覆盖）	    DownloadManager::resolveLocalPath / ensureDirExists
msi/exe/bat 以 TI 权限执行	    InstallManager::executeMsi/Exe/Bat + TIHelperWrapper
多文件复制	                    sources[] 中 kind=file
目录复制	                            sources[] 中 kind=dir + files[]
ZIP 解压到指定位置	                    sources[] 中 kind=zip + FileOperationManager::extractZip
复制后执行注册命令	                    type=copy_and_register + registerCommands[]
多进程强杀（含兜底 wmic）	            ProcessManager::killProcessByName
多服务停止（含 PENDING 强杀宿主）	    ProcessManager::stopServiceByName
安装前先停服务再杀进程	            InstallManager::installItem 第②步
JSON 动态扩展（无需改代码）	            修改 config_ftp.json / config_http.json
Win7~Win11 兼容	                    app.manifest + /SUBSYSTEM:WINDOWS,6.01 + 静态打包
