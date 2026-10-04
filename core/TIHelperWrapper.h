#ifndef TIHELPERWRAPPER_H
#define TIHELPERWRAPPER_H

#include <QString>
#include <windows.h>

typedef int (*ti_execute_file_fn)(const char *exec_path);

class TIHelperWrapper
{
public:
    static TIHelperWrapper &instance();

    bool load();
    void unload();
    bool isLoaded() const { return m_hDll != nullptr; }

    // 以 TrustedInstaller 权限执行指定文件，返回 0 表示成功
    int executeFile(const QString &filePath);

private:
    TIHelperWrapper() = default;
    ~TIHelperWrapper();
    TIHelperWrapper(const TIHelperWrapper &) = delete;
    TIHelperWrapper &operator=(const TIHelperWrapper &) = delete;

    HMODULE m_hDll = nullptr;
    ti_execute_file_fn m_fn = nullptr;
};

#endif
