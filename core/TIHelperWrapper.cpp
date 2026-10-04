#include "TIHelperWrapper.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

TIHelperWrapper &TIHelperWrapper::instance()
{
    static TIHelperWrapper s;
    return s;
}

TIHelperWrapper::~TIHelperWrapper() { unload(); }

bool TIHelperWrapper::load()
{
    if (m_hDll) return true;

    QString exeDir = QCoreApplication::applicationDirPath();
    QString dllPath = QDir::toNativeSeparators(exeDir + "/TIHelper.dll");
    qDebug() << "尝试加载 TIHelper.dll:" << dllPath;

    m_hDll = LoadLibraryExW(
        reinterpret_cast<LPCWSTR>(dllPath.utf16()),
        nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

    if (!m_hDll) {
        qDebug() << "exe 目录未找到，回退到系统搜索";
        m_hDll = LoadLibraryW(L"TIHelper.dll");
    }
    if (!m_hDll) {
        qWarning() << "LoadLibrary 失败, error code:" << GetLastError();
        return false;
    }

    m_fn = reinterpret_cast<ti_execute_file_fn>(
        GetProcAddress(m_hDll, "ti_execute_file"));

    if (!m_fn) {
        qWarning() << "GetProcAddress(ti_execute_file) 失败, error:"
                   << GetLastError();
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        return false;
    }

    qDebug() << "TIHelper.dll 加载成功";
    return true;
}

void TIHelperWrapper::unload()
{
    if (m_hDll) {
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        m_fn = nullptr;
    }
}

int TIHelperWrapper::executeFile(const QString &filePath)
{
    if (!m_hDll || !m_fn) {
        qWarning() << "TIHelper.dll 未加载，无法执行:" << filePath;
        return -1;
    }
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        qWarning() << "待执行文件不存在:" << filePath;
        return -2;
    }

    QString nativePath = QDir::toNativeSeparators(fi.absoluteFilePath());
    QByteArray utf8 = nativePath.toUtf8();

    qDebug() << "ti_execute_file:" << nativePath;
    int ret = m_fn(utf8.constData());
    qDebug() << "ti_execute_file 返回值:" << ret;
    return ret;
}
