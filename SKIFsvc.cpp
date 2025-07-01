#include <Windows.h>
#include <shlwapi.h>
#include <string>
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
    LPWSTR buffer = nullptr;
    DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, lastError, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring msg(buffer ? buffer : L"", size);
    LocalFree(buffer);
    msg.erase(std::remove(msg.begin(), msg.end(), L'\n'), msg.end());

    std::wstring full = preMsg;
    if (!preMsg.empty()) full += L"\n\n";
    full += L"[" + std::to_wstring(lastError) + L"] " + msg;

    MessageBoxW(nullptr, full.c_str(), winTitle.c_str(), MB_OK | MB_ICONERROR);
}

// Format a wide string safely
std::wstring SK_FormatStringW(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    wchar_t stackBuf[4096];
    int len = vswprintf_s(stackBuf, std::size(stackBuf), fmt, args);
    va_end(args);

    if (len >= 0 && len < static_cast<int>(std::size(stackBuf)))
        return std::wstring(stackBuf, len);

    va_start(args, fmt);
    int required = _vscwprintf(fmt, args);
    va_end(args);
    if (required <= 0) return {};

    std::wstring result(required + 1, L'\0');
    va_start(args, fmt);
    vswprintf_s(result.data(), result.size(), fmt, args);
    va_end(args);
    result.resize(lstrlenW(result.c_str()));

    return result;
}

// Type for DLL entry point
using DLL_t = void(WINAPI*)(HWND, HINSTANCE, LPCSTR, int);
constexpr char kCmdInstall[] = "Install";
constexpr char kCmdRemove[]  = "Remove";

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    std::wstring cmd(lpCmdLine ? lpCmdLine : L"");
    bool doStop  = (cmd.find(L"Stop") != std::wstring::npos);
    bool doStart = (cmd.find(L"Start") != std::wstring::npos);
#ifndef _WIN64
    bool proxy64 = (cmd.find(L"Proxy64") != std::wstring::npos);
#endif

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
    PathRemoveFileSpecW(buf.data());
    std::wstring exeFolder(buf.data());

    if (StrStrIW(curDir.c_str(), L"\\Windows\\Sys"))
        SetCurrentDirectoryW(exeFolder.c_str());

#if _WIN64
    std::wstring dllFile = L"SpecialK64.dll";
#else
    std::wstring dllFile = L"SpecialK32.dll";
#endif

    std::vector<std::wstring> candidates = {
        dllFile,
        L"..\\" + dllFile,
        SK_FormatStringW(LR"(%ws\%ws)", exeFolder.c_str(), dllFile.c_str()),
        SK_FormatStringW(LR"(%ws\..\%ws)", exeFolder.c_str(), dllFile.c_str())
    };

    std::wstring dllPath;
    for (const auto& p : candidates) {
        if (FileExists(p.c_str())) {
            dllPath = p;
            break;
        }
    }

    DWORD lastError = NO_ERROR;
    SetLastError(NO_ERROR);

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

        if (doStop)
            fn(nullptr, nullptr, kCmdRemove, SW_HIDE);
        else if (doStart)
            fn(nullptr, nullptr, kCmdInstall, SW_HIDE);
        else {
#if _WIN64
            LPCWSTR eventName = LR"(Local\SK_GlobalHookTeardown64)";
#else
            LPCWSTR eventName = LR"(Local\SK_GlobalHookTeardown32)";
#endif
            HANDLE hEvt = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
            if (hEvt) {
                SetEvent(hEvt);
                CloseHandle(hEvt);
            } else {
                fn(nullptr, nullptr, kCmdInstall, SW_HIDE);
            }
        }

        Sleep(50);
        FreeLibrary(hMod);
    } else {
        lastError = GetLastError();
        ShowErrorMessage(lastError, L"Failed to load " + dllFile, L"SKIFsvc");
    }

    return lastError;
}
