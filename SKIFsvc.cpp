#include <Windows.h>
#include <shlwapi.h>
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
