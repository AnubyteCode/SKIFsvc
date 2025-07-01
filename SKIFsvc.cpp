#include <Windows.h>
#include <shlwapi.h>        // For PathRemoveFileSpecW and StrStrIW
#include <string>
#include <memory>
#include <algorithm>
#include <vector>

// Checks if a file exists and is not a directory
BOOL FileExists(LPCTSTR szPath)
{
    DWORD dwAttrib = GetFileAttributes(szPath);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void ShowErrorMessage(DWORD lastError, const std::wstring& preMsg = L"", const std::wstring& winTitle = L"")
{
    LPWSTR messageBuffer = nullptr;
    size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
    std::wstring message(messageBuffer, size);
    LocalFree(messageBuffer);

    message.erase(std::remove(message.begin(), message.end(), L'\n'), message.end());
    std::wstring fullMsg = preMsg;
    if (!preMsg.empty())
        fullMsg += L"\n\n";
    fullMsg += L"[" + std::to_wstring(lastError) + L"] " + message;

    MessageBox(nullptr, fullMsg.c_str(), winTitle.c_str(), MB_OK | MB_ICONERROR);
}

// Fixed-size buffer version of SK_FormatStringW (assuming 4096 is enough)
std::wstring SK_FormatStringW(wchar_t const* const _Format, ...)
{
    va_list args;
    va_start(args, _Format);

    // First, attempt to format into a fixed-size buffer on the stack
    wchar_t buffer[4096];
    int len = vswprintf_s(buffer, _countof(buffer), _Format, args);

    va_end(args);

    if (len >= 0 && len < _countof(buffer))
    {
        return std::wstring(buffer, len); // Return without heap allocation
    }

    // If buffer is too small or formatting fails, determine required size
    va_start(args, _Format);
    len = _vscwprintf(_Format, args) + 1; // _vscwprintf gets required length
    va_end(args);

    if (len <= 0) return L""; // Formatting failed, return empty string

    // Allocate correct buffer size
    std::wstring result(len, L'\0');

    va_start(args, _Format);
    vswprintf_s(result.data(), len, _Format, args);
    va_end(args);

    return result;
}


using DLL_t = void(WINAPI*)(HWND hwnd, HINSTANCE hInst, LPCSTR lpszCmdLine, int nCmdShow);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Process command-line parameters using std::wstring::find
    struct SKIFsvc_Signals
    {
        BOOL Stop = FALSE;
        BOOL Start = FALSE;
#ifndef _WIN64
        BOOL Proxy64 = FALSE;
#endif
    } _Signal;

    std::wstring cmdLine(lpCmdLine ? lpCmdLine : L"");
    _Signal.Stop = (cmdLine.find(L"Stop") != std::wstring::npos);
    _Signal.Start = (cmdLine.find(L"Start") != std::wstring::npos);
#ifndef _WIN64
    _Signal.Proxy64 = (cmdLine.find(L"Proxy64") != std::wstring::npos);
#endif

    // Retrieve current directory dynamically to avoid buffer overflow
    DWORD curDirSize = GetCurrentDirectory(0, nullptr);
    std::wstring wszCurrentPath;
    if (curDirSize > 0)
    {
        wszCurrentPath.resize(curDirSize);
        GetCurrentDirectory(curDirSize, &wszCurrentPath[0]);
        wszCurrentPath.resize(wcslen(wszCurrentPath.c_str())); // trim extra nulls
    }
    else
    {
        ShowErrorMessage(GetLastError(), L"Failed to get current directory", L"SKIFsvc");
        return GetLastError();
    }

    // Retrieve the executable path dynamically
    HMODULE hModSelf = GetModuleHandleW(nullptr);
    DWORD exePathSize = MAX_PATH;
    std::wstring wszExeFolder;
    {
        std::vector<wchar_t> exePathBuffer(exePathSize);
        DWORD len = GetModuleFileNameW(hModSelf, exePathBuffer.data(), exePathSize);
        // If the buffer is too small, double its size and try again
        while (len == exePathSize && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            exePathSize *= 2;
            exePathBuffer.resize(exePathSize);
            len = GetModuleFileNameW(hModSelf, exePathBuffer.data(), exePathSize);
        }
        if (len == 0)
        {
            ShowErrorMessage(GetLastError(), L"Failed to get module file name", L"SKIFsvc");
            return GetLastError();
        }
        wszExeFolder = exePathBuffer.data();
        PathRemoveFileSpecW(&wszExeFolder[0]); // remove the file part to get the folder
        wszExeFolder.resize(wcslen(wszExeFolder.c_str()));
    }

    // Change working directory if we're in a system directory
    if (wszCurrentPath.find(L"\\Windows\\sys") != std::wstring::npos)
    {
        SetCurrentDirectory(wszExeFolder.c_str());
    }

    // Determine the DLL file name based on architecture
#if _WIN64
    std::wstring wsDllFile = L"SpecialK64.dll";
#else
    std::wstring wsDllFile = L"SpecialK32.dll";
#endif

    std::wstring wsDllPath;
    std::wstring wsTestPaths[] = {
        wsDllFile,
        LR"(..\)" + wsDllFile,
        SK_FormatStringW(LR"(%ws\%ws)", wszExeFolder.c_str(), wsDllFile.c_str()),
        SK_FormatStringW(LR"(%ws\..\%ws)", wszExeFolder.c_str(), wsDllFile.c_str())
    };

    for (auto& path : wsTestPaths)
    {
        if (FileExists(path.c_str()))
        {
            wsDllPath = path;
            break;
        }
    }
#include <vector>
#include <algorithm>
#include <cstdarg>

// Helper: check if a file exists and is not a directory
inline bool FileExists(LPCWSTR path)
{
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

// Display a Win32 error message box with optional prefix and title
void ShowErrorMessage(DWORD lastError, const std::wstring& preMsg = L"", const std::wstring& winTitle = L"Error")
{
    // Retrieve formatted system error message
    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, lastError, 0,
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring msg(buffer ? buffer : L"", size);
    LocalFree(buffer);

    // Strip out any stray newlines
    msg.erase(std::remove(msg.begin(), msg.end(), L'\n'), msg.end());

    // Build full message: optional prefix, error code + text
    std::wstring full = preMsg;
    if (!preMsg.empty())
        full += L"\n\n";
    full += L"[" + std::to_wstring(lastError) + L"] " + msg;

    MessageBoxW(nullptr, full.c_str(), winTitle.c_str(), MB_OK | MB_ICONERROR);
}

// Format a wide string safely, using a stack buffer first, then heap if needed
std::wstring SK_FormatStringW(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    // Try stack buffer
    wchar_t stackBuf[4096];
    int len = vswprintf_s(stackBuf, std::size(stackBuf), fmt, args);
    va_end(args);

    if (len >= 0 && len < (int)std::size(stackBuf))
        return std::wstring(stackBuf, len);

    // Need larger buffer: compute required length
    va_start(args, fmt);
    int required = _vscwprintf(fmt, args);
    va_end(args);
    if (required <= 0)
        return {};

    // Allocate and format
    std::wstring result(required + 1, L'\0');
    va_start(args, fmt);
    vswprintf_s(result.data(), result.size(), fmt, args);
    va_end(args);

    // Trim trailing null
    result.resize(lstrlenW(result.c_str()));
    return result;
}
// Type for the injection function
using DLL_t = void(WINAPI*)(HWND, HINSTANCE, LPCSTR, int);

// Command strings for injection manager
constexpr char kCmdInstall[] = "Install";
constexpr char kCmdRemove[]  = "Remove";

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow)
{
    // Parse command-line for keywords
    std::wstring cmd(lpCmdLine ? lpCmdLine : L"");
    bool doStop   = (cmd.find(L"Stop")   != std::wstring::npos);
    bool doStart  = (cmd.find(L"Start")  != std::wstring::npos);
#ifndef _WIN64
    bool proxy64  = (cmd.find(L"Proxy64")!= std::wstring::npos);
#endif
    // Get current directory safely
    DWORD curSize = GetCurrentDirectoryW(0, nullptr);
    std::wstring curDir;
    if (curSize > 0) {
        curDir.resize(curSize);
        GetCurrentDirectoryW(curSize, curDir.data());
        curDir.resize(lstrlenW(curDir.c_str()));
    } else {
        ShowErrorMessage(GetLastError(), L"Failed to get current directory");
        return GetLastError();
    }

    // Get this executable's folder path
    HMODULE hSelf = GetModuleHandleW(nullptr);
    DWORD bufSize = MAX_PATH;
    std::vector<wchar_t> buf(bufSize);
    DWORD len = GetModuleFileNameW(hSelf, buf.data(), bufSize);
    while (len == bufSize && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        bufSize *= 2;
        buf.resize(bufSize);
        len = GetModuleFileNameW(hSelf, buf.data(), bufSize);
    }
    if (len == 0) {
        ShowErrorMessage(GetLastError(), L"Failed to get module file name");
        return GetLastError();
    }
    // Remove filename, leaving folder
    PathRemoveFileSpecW(buf.data());
    std::wstring exeFolder(buf.data());

    // If running from a system folder, switch to exe folder
    if (StrStrIW(curDir.c_str(), L"\\Windows\\Sys"))
        SetCurrentDirectoryW(exeFolder.c_str());

    // Decide DLL name based on architecture
    std::wstring dllFile = 
    #if _WIN64
        L"SpecialK64.dll";
    #else
        L"SpecialK32.dll";
    #endif

    // Potential locations to search for the DLL
    std::vector<std::wstring> candidates = {
        dllFile,
        L"..\\" + dllFile,
        SK_FormatStringW(LR"(%ws\%ws)", exeFolder.c_str(), dllFile.c_str()),
        SK_FormatStringW(LR"(%ws\..\%ws)", exeFolder.c_str(), dllFile.c_str())
    };

    // Pick the first existing path
    std::wstring dllPath;
    auto it = std::find_if(candidates.begin(), candidates.end(),
        [](auto& p){ return FileExists(p.c_str()); });
    if (it != candidates.end())
        dllPath = *it;

    DWORD lastError = NO_ERROR;
    SetLastError(NO_ERROR);

    auto SKModule = LoadLibrary(wsDllPath.c_str());
    if (SKModule)
    {
        auto RunDLL_InjectionManager = (DLL_t)GetProcAddress(SKModule, "RunDLL_InjectionManager");
        if (!RunDLL_InjectionManager)
        {
            lastError = GetLastError();
            ShowErrorMessage(lastError, L"Failed to locate RunDLL_InjectionManager in " + wsDllFile, L"SKIFsvc");
            FreeLibrary(SKModule);
            return lastError;
        }

        if (_Signal.Stop)
            RunDLL_InjectionManager(0, 0, "Remove", SW_HIDE);
        else if (_Signal.Start)
            RunDLL_InjectionManager(0, 0, "Install", SW_HIDE);
        else
        {
#if _WIN64
            HANDLE hService = OpenEvent(EVENT_MODIFY_STATE, FALSE, LR"(Local\SK_GlobalHookTeardown64)");
#else
            HANDLE hService = OpenEvent(EVENT_MODIFY_STATE, FALSE, LR"(Local\SK_GlobalHookTeardown32)");
#endif
            if (hService != nullptr)
            {
                SetEvent(hService);
                CloseHandle(hService);
            }
            else
            {
                RunDLL_InjectionManager(0, 0, "Install", SW_HIDE);
            }
        }

        Sleep(50);
        FreeLibrary(SKModule);
    }
    else
    {
        lastError = GetLastError();
        ShowErrorMessage(lastError, L"There was a problem starting " + wsDllFile, L"SKIFsvc");
    }

    return lastError;
}
    // Load the DLL and locate the injection entrypoint
    HMODULE hMod = LoadLibraryW(dllPath.c_str());
    if (hMod) {
        auto fn = reinterpret_cast<DLL_t>(
            GetProcAddress(hMod, "RunDLL_InjectionManager"));
        if (!fn) {
            lastError = GetLastError();
            ShowErrorMessage(lastError,
                L"Failed to find RunDLL_InjectionManager in " + dllFile);
            FreeLibrary(hMod);
            return lastError;
        }

        // Invoke based on signals
        if (doStop)
            fn(nullptr, nullptr, kCmdRemove, SW_HIDE);
        else if (doStart)
            fn(nullptr, nullptr, kCmdInstall, SW_HIDE);
        else {
            // No explicit start/stop: signal existing service or install
            LPCWSTR eventName = 
            #if _WIN64
                LR"(Local\SK_GlobalHookTeardown64)";
            #else
                LR"(Local\SK_GlobalHookTeardown32)";
            #endif
            HANDLE hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
            if (hEvt) {
                SetEvent(hEvt);
                CloseHandle(hEvt);
            } else {
                fn(nullptr, nullptr, kCmdInstall, SW_HIDE);
            }
        }

        Sleep(50);  // give the injection manager time to act
        FreeLibrary(hMod);
    }
    else {
        lastError = GetLastError();
        ShowErrorMessage(lastError,
            L"Failed to load " + dllFile, L"SKIFsvc");
    }

    return lastError;
}
