#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cwctype>

#include "config.hpp"

namespace {

constexpr wchar_t kSunbirdHost[] = L"assets.sunbird.tools";
constexpr wchar_t kVersionPath[] = L"/version.json";
constexpr wchar_t kDllPath[] = L"/plugins/phoenix-windows-x86_64.dll";

void Alert(const std::wstring& msg, UINT icon = MB_ICONERROR) {
    MessageBoxW(nullptr, msg.c_str(), L"Phoenix Themes", MB_OK | icon);
}

bool AskYesNo(const std::wstring& msg) {
    return MessageBoxW(nullptr, msg.c_str(), L"Phoenix Themes", MB_YESNO | MB_ICONQUESTION) == IDYES;
}

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetExePath() {
    std::wstring buf(512, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size() - 1) {
            buf.resize(n);
            return buf;
        }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
}

std::wstring GetExeDir() {
    auto p = GetExePath();
    size_t slash = p.find_last_of(L"\\/");
    return (slash != std::wstring::npos) ? p.substr(0, slash) : L".";
}

// Standard Windows CreateProcessW / CommandLineToArgvW quoting rules
std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (size_t i = 0; i < arg.size(); ++i) {
        size_t numBackslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++numBackslashes;
        }
        if (i == arg.size()) {
            out.append(numBackslashes * 2, L'\\');
            break;
        } else if (arg[i] == L'"') {
            out.append(numBackslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(numBackslashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

bool SafeCopyFile(const std::wstring& src, const std::wstring& dst) {
    if (_wcsicmp(src.c_str(), dst.c_str()) == 0) return FileExists(dst);
    if (!FileExists(src)) return FileExists(dst);
    size_t slash = dst.find_last_of(L"\\/");
    if (slash != std::wstring::npos) phoenix_themes::EnsureDirRecursive(dst.substr(0, slash));
    if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) return true;
    std::wstring old = dst + L".old";
    DeleteFileW(old.c_str());
    if (MoveFileExW(dst.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
            DeleteFileW(old.c_str());
            return true;
        }
        // Roll back on copy failure so existing binary is never lost
        MoveFileExW(old.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    return false;
}

bool WriteShortcut(const std::wstring& lnkPath, const std::wstring& targetPath,
                   const std::wstring& iconPath, const std::wstring& desc) {
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
    if (FAILED(hr) || !psl) return false;
    psl->SetPath(targetPath.c_str());
    psl->SetDescription(desc.c_str());
    psl->SetIconLocation(iconPath.c_str(), 0);
    IPersistFile* ppf = nullptr;
    bool ok = false;
    if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf))) && ppf) {
        ok = SUCCEEDED(ppf->Save(lnkPath.c_str(), TRUE));
        ppf->Release();
    }
    psl->Release();
    return ok;
}

void UpdateShortcuts(const std::wstring& targetExe, const std::wstring& desc) {
    std::wstring iconPath = phoenix_themes::GetLocalAppData() + L"\\DiscordPhoenix\\app\\Discord.exe";
    KNOWNFOLDERID folders[2] = {FOLDERID_Desktop, FOLDERID_Programs};
    for (const auto& fid : folders) {
        PWSTR dir = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(fid, 0, nullptr, &dir)) && dir) {
            WriteShortcut(std::wstring(dir) + L"\\Sunbird.lnk", targetExe, iconPath, desc);
            CoTaskMemFree(dir);
        }
    }
}

void RemoveShortcuts() {
    KNOWNFOLDERID folders[2] = {FOLDERID_Desktop, FOLDERID_Programs};
    for (const auto& fid : folders) {
        PWSTR dir = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(fid, 0, nullptr, &dir)) && dir) {
            DeleteFileW((std::wstring(dir) + L"\\Sunbird.lnk").c_str());
            CoTaskMemFree(dir);
        }
    }
}

bool HttpGetBytes(const wchar_t* host, const wchar_t* path, std::string& outBody, DWORD timeoutMs = 15000) {
    outBody.clear();
    HINTERNET hSession = WinHttpOpen(L"PhoenixThemes/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0, statusSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
            statusCode == 200) {
            // Optional Content-Length check
            wchar_t clBuf[64] = {};
            DWORD clSize = sizeof(clBuf);
            bool hasContentLength = false;
            size_t expectedLength = 0;
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX, clBuf, &clSize, WINHTTP_NO_HEADER_INDEX)) {
                wchar_t* end = nullptr;
                unsigned long long parsed = wcstoull(clBuf, &end, 10);
                if (end && end != clBuf && parsed <= 64ULL * 1024 * 1024) {
                    hasContentLength = true;
                    expectedLength = static_cast<size_t>(parsed);
                }
            }

            constexpr size_t kMaxBody = 64 * 1024 * 1024;
            bool cleanEof = false;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
                    outBody.clear();
                    break;
                }
                if (avail == 0) {
                    cleanEof = true;
                    break;
                }
                if (avail > kMaxBody || outBody.size() > kMaxBody - avail) {
                    outBody.clear();
                    break;
                }
                size_t oldSz = outBody.size();
                outBody.resize(oldSz + avail);
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, outBody.data() + oldSz, avail, &read)) {
                    outBody.clear();
                    break;
                }
                outBody.resize(oldSz + read);
                if (read == 0) {
                    cleanEof = true;
                    break;
                }
            }

            if (cleanEof && !outBody.empty() && (!hasContentLength || outBody.size() == expectedLength)) {
                ok = true;
            } else {
                outBody.clear();
            }
        }
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

bool ValidatePe64Dll(const std::string& bytes) {
    if (bytes.size() < sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS64)) return false;
    IMAGE_DOS_HEADER dos{};
    memcpy(&dos, bytes.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (dos.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))) return false;
    size_t ntOff = static_cast<size_t>(dos.e_lfanew);
    if (ntOff > bytes.size() - sizeof(IMAGE_NT_HEADERS64)) return false;

    IMAGE_NT_HEADERS64 nt{};
    memcpy(&nt, bytes.data() + ntOff, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE) return false;
    if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;
    if ((nt.FileHeader.Characteristics & IMAGE_FILE_DLL) == 0) return false;

    size_t secOff = ntOff + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    size_t secCount = nt.FileHeader.NumberOfSections;
    if (secCount == 0 || secOff > bytes.size() || secCount > (bytes.size() - secOff) / sizeof(IMAGE_SECTION_HEADER)) {
        return false;
    }
    for (size_t i = 0; i < secCount; ++i) {
        IMAGE_SECTION_HEADER sec{};
        memcpy(&sec, bytes.data() + secOff + i * sizeof(IMAGE_SECTION_HEADER), sizeof(sec));
        if (sec.SizeOfRawData > 0) {
            if (static_cast<size_t>(sec.PointerToRawData) > bytes.size() ||
                static_cast<size_t>(sec.SizeOfRawData) > bytes.size() - static_cast<size_t>(sec.PointerToRawData)) {
                return false;
            }
        }
    }
    return true;
}

std::string ReadSmallFile(const std::wstring& path) {
    std::string out;
    if (!phoenix_themes::ReadTextFile(path, out)) return {};
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

void TryUpdateSunbirdDll(const std::wstring& sunbirdDir) {
    phoenix_themes::EnsureDirRecursive(sunbirdDir);
    std::wstring dllPath = sunbirdDir + L"\\phoenix.dll";
    std::wstring stampPath = sunbirdDir + L"\\version.txt";

    std::string manifest;
    if (!HttpGetBytes(kSunbirdHost, kVersionPath, manifest, 8000)) return;
    std::string latestVer;
    if (!phoenix_themes::ExtractJsonString(manifest, "loader", latestVer) || latestVer.empty()) return;
    if (FileExists(dllPath) && ReadSmallFile(stampPath) == latestVer) return;

    std::string dllBytes;
    if (!HttpGetBytes(kSunbirdHost, kDllPath, dllBytes, 45000)) return;
    if (!ValidatePe64Dll(dllBytes)) return;

    std::wstring freshPath = sunbirdDir + L"\\phoenix.dll." + std::to_wstring(GetCurrentProcessId()) + L".new";
    if (!phoenix_themes::WriteTextFile(freshPath, dllBytes)) {
        DeleteFileW(freshPath.c_str());
        return;
    }

    std::wstring oldPath = sunbirdDir + L"\\phoenix.dll.old";
    DeleteFileW(oldPath.c_str());
    bool hadExisting = FileExists(dllPath);
    if (hadExisting && !MoveFileExW(dllPath.c_str(), oldPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(freshPath.c_str());
        return;
    }
    if (!MoveFileExW(freshPath.c_str(), dllPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (hadExisting) MoveFileExW(oldPath.c_str(), dllPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(freshPath.c_str());
        return;
    }
    DeleteFileW(oldPath.c_str());
    phoenix_themes::WriteTextFile(stampPath, latestVer);
}

struct RunningProc { HANDLE handle; DWORD pid; };

std::vector<RunningProc> FindRunning(const std::wstring& exePath) {
    std::vector<RunningProc> result;
    std::wstring want = ToLower(exePath);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Discord.exe") == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (h) {
                    wchar_t buf[1024] = {};
                    DWORD len = 1024;
                    if (QueryFullProcessImageNameW(h, 0, buf, &len) && ToLower(std::wstring(buf, len)) == want) {
                        result.push_back({h, pe.th32ProcessID});
                    } else {
                        CloseHandle(h);
                    }
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return result;
}

bool HasModule(DWORD pid, const wchar_t* modName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, modName) == 0) { found = true; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

bool QueueLoadDll(const PROCESS_INFORMATION& pi, const std::wstring& dllPath) {
    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;
    if (!WriteProcessMemory(pi.hProcess, remote, dllPath.c_str(), bytes, nullptr)) {
        VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
        return false;
    }
    auto loadLib = reinterpret_cast<PAPCFUNC>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadLib) {
        VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
        return false;
    }
    if (QueueUserAPC(loadLib, pi.hThread, reinterpret_cast<ULONG_PTR>(remote)) == 0) {
        VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

void InstallCoreAndAdapters() {
    std::wstring exeDir = GetExeDir();
    std::wstring baseDir = phoenix_themes::GetBaseDir();
    std::wstring installedExe = baseDir + L"\\phoenix-themes.exe";
    std::wstring installedDll = baseDir + L"\\phoenix-themes.dll";
    phoenix_themes::EnsureDirRecursive(baseDir);

    if (_wcsicmp(GetExePath().c_str(), installedExe.c_str()) != 0) {
        SafeCopyFile(GetExePath(), installedExe);
        SafeCopyFile(exeDir + L"\\phoenix-themes.dll", installedDll);
        UpdateShortcuts(installedExe, L"Discord Phoenix with Phoenix Themes");
    }

    // Install thin Sunbird settings UI adapter
    std::wstring srcPlugin = exeDir + L"\\adapters\\sunbird\\phoenix-themes.js";
    if (!FileExists(srcPlugin)) srcPlugin = exeDir + L"\\phoenix-themes.js";
    if (FileExists(srcPlugin)) {
        SafeCopyFile(srcPlugin, phoenix_themes::GetSunbirdAdapterPluginPath());
    }

    // Seed bundled .theme.css themes into %APPDATA%\phoenix-themes\themes (never overwriting user edits)
    std::wstring srcThemesDir = exeDir + L"\\themes";
    std::wstring dstThemesDir = phoenix_themes::GetThemesDir();
    phoenix_themes::EnsureDirRecursive(dstThemesDir);
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW((srcThemesDir + L"\\*.theme.css").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring dst = dstThemesDir + L"\\" + fd.cFileName;
                if (!FileExists(dst)) SafeCopyFile(srcThemesDir + L"\\" + fd.cFileName, dst);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

void Uninstall() {
    std::wstring discordExe = phoenix_themes::GetLocalAppData() + L"\\DiscordPhoenix\\app\\Discord.exe";
    auto open = FindRunning(discordExe);
    bool running = !open.empty();
    for (const auto& p : open) CloseHandle(p.handle);
    if (running) {
        Alert(L"Please completely quit Discord Phoenix before running --uninstall.", MB_ICONWARNING);
        return;
    }

    std::wstring origSunbird = phoenix_themes::GetSunbirdBinDir() + L"\\sunbird.exe";
    if (FileExists(origSunbird)) {
        UpdateShortcuts(origSunbird, L"Discord with Sunbird");
    } else {
        RemoveShortcuts();
    }

    DeleteFileW(phoenix_themes::GetSunbirdAdapterPluginPath().c_str());
    std::wstring dllPath = phoenix_themes::GetBaseDir() + L"\\phoenix-themes.dll";
    bool dllOk = !FileExists(dllPath) || (DeleteFileW(dllPath.c_str()) != 0);
    DeleteFileW(phoenix_themes::GetLogPath().c_str());

    if (!dllOk) {
        Alert(L"Could not remove phoenix-themes.dll because it is still in use.\n"
              L"Please close Discord Phoenix completely and run --uninstall again.", MB_ICONWARNING);
        return;
    }
    Alert(L"Phoenix Themes has been uninstalled.\nYour themes in %APPDATA%\\phoenix-themes\\themes were kept safe.", MB_ICONINFORMATION);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool forceRestart = false, doUninstall = false, noUpdate = false, noSunbird = false;
    std::wstring forwardArgs;
    for (int i = 1; i < argc; ++i) {
        std::wstring a(argv[i]);
        if (a == L"--force-restart") forceRestart = true;
        else if (a == L"--uninstall") doUninstall = true;
        else if (a == L"--no-update") noUpdate = true;
        else if (a == L"--no-sunbird") noSunbird = true;
        else {
            if (!forwardArgs.empty()) forwardArgs += L" ";
            forwardArgs += QuoteArg(a);
        }
    }
    if (argv) LocalFree(argv);

    if (doUninstall) {
        Uninstall();
        CoUninitialize();
        return 0;
    }

    std::wstring local = phoenix_themes::GetLocalAppData();
    std::wstring appDir = local + L"\\DiscordPhoenix\\app";
    std::wstring discordExe = appDir + L"\\Discord.exe";
    if (!FileExists(discordExe)) {
        Alert(L"Discord Phoenix is not installed at:\n" + discordExe);
        CoUninitialize();
        return 1;
    }

    InstallCoreAndAdapters();

    auto cfg = phoenix_themes::LoadConfig();
    bool wantSunbird = cfg.use_sunbird && !noSunbird;
    if (noSunbird) {
        SetEnvironmentVariableW(L"PHOENIX_THEMES_NO_SUNBIRD", L"1");
    }

    std::wstring sunbirdDir = phoenix_themes::GetSunbirdBinDir();
    std::wstring phoenixDll = sunbirdDir + L"\\phoenix.dll";
    std::wstring themesDll = phoenix_themes::GetBaseDir() + L"\\phoenix-themes.dll";
    if (!FileExists(themesDll)) themesDll = GetExeDir() + L"\\phoenix-themes.dll";

    auto open = FindRunning(discordExe);
    if (!open.empty()) {
        bool injectThemesNow = FileExists(themesDll);
        bool injectSunbirdNow = wantSunbird && FileExists(phoenixDll);
        bool hasPhoenix = !injectSunbirdNow;
        bool hasThemes = !injectThemesNow;
        for (const auto& p : open) {
            if (injectSunbirdNow && HasModule(p.pid, L"phoenix.dll")) hasPhoenix = true;
            if (injectThemesNow && HasModule(p.pid, L"phoenix-themes.dll")) hasThemes = true;
        }
        bool ready = hasPhoenix && hasThemes;
        bool restart = forceRestart || (!ready && AskYesNo(
            L"Discord Phoenix is already running without Phoenix Themes.\n\nRestart it now?"));
        for (const auto& p : open) { if (restart) TerminateProcess(p.handle, 0); }
        for (const auto& p : open) { if (restart) WaitForSingleObject(p.handle, 5000); CloseHandle(p.handle); }
        if (!restart) {
            if (ready) {
                STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
                std::wstring cmd = QuoteArg(discordExe);
                if (!forwardArgs.empty()) cmd += L" " + forwardArgs;
                if (CreateProcessW(discordExe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, appDir.c_str(), &si, &pi)) {
                    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                }
            }
            CoUninitialize();
            return 0;
        }
        Sleep(400);
    }

    // Update Sunbird runtime AFTER stopping any running Discord instance so phoenix.dll is unlocked
    if (wantSunbird && cfg.auto_update_sunbird && !noUpdate) {
        TryUpdateSunbirdDll(sunbirdDir);
    }

    bool injectThemes = FileExists(themesDll);
    bool injectSunbird = wantSunbird && FileExists(phoenixDll);

    std::wstring cmd = QuoteArg(discordExe);
    if (!forwardArgs.empty()) cmd += L" " + forwardArgs;

    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    if (!CreateProcessW(discordExe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, appDir.c_str(), &si, &pi)) {
        Alert(L"Failed to launch Discord Phoenix.");
        CoUninitialize();
        return 1;
    }

    bool ok = true;
    if (injectThemes) ok = ok && QueueLoadDll(pi, themesDll);
    if (injectSunbird) ok = ok && QueueLoadDll(pi, phoenixDll);
    if (!ok) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        Alert(L"Failed to queue DLL injection.");
        CoUninitialize();
        return 1;
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CoUninitialize();
    return 0;
}
