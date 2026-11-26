#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwrite_3.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_set>
#include <vector>

#include "config.hpp"
#include "theme_css.hpp"
#include "wallpaper_d3d.hpp"
#include "d3d_theme_hook.hpp"

namespace {

// Verified against Windows SDK 10.0.26100.0 (dwrite.h / dwrite_1.h / dwrite_2.h):
constexpr size_t kIdxCreateTextFormat = 15;
constexpr size_t kIdxGetSystemFontFallback = 26;

constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFFu;
constexpr COLORREF kDwmColorNone = 0xFFFFFFFEu;

using PFN_DWriteCreateFactory = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
using PFN_CreateTextFormat = HRESULT(STDMETHODCALLTYPE*)(
    IDWriteFactory*, WCHAR const*, IDWriteFontCollection*, DWRITE_FONT_WEIGHT,
    DWRITE_FONT_STYLE, DWRITE_FONT_STRETCH, FLOAT, WCHAR const*, IDWriteTextFormat**);
using PFN_GetSystemFontFallback = HRESULT(STDMETHODCALLTYPE*)(
    IDWriteFactory2*, IDWriteFontFallback**);
using PFN_CreateProcessW = BOOL(WINAPI*)(
    LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
    LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

HMODULE g_selfModule = nullptr;
std::wstring g_selfDllPath;
std::wstring g_selfExePath;

PFN_DWriteCreateFactory g_origDWriteCreateFactory = nullptr;
PFN_CreateTextFormat g_origCreateTextFormat = nullptr;
PFN_GetSystemFontFallback g_origGetSystemFontFallback = nullptr;
PFN_CreateProcessW g_origCreateProcessW = nullptr;

SRWLOCK g_lock = SRWLOCK_INIT;
SRWLOCK g_fileLock = SRWLOCK_INIT;
bool g_vtableHooked = false;
bool g_customFontAttempted = false;
IUnknown* g_customFontFactoryId = nullptr;
IDWriteFontFallback* g_customFallback = nullptr;
IUnknown* g_customFallbackFactoryId = nullptr;
IDWriteFontCollection1* g_customFontCollection = nullptr;
std::wstring g_customFontFamily;
volatile LONG g_loggedFirstTextFormat = 0;
volatile LONG g_loggedFirstWindow = 0;
std::unordered_set<HWND> g_layeredHwnds;

phoenix_themes::Config g_cfg;
volatile LONG g_stopWatcher = 0;
volatile LONG g_dialogBusy = 0;
HANDLE g_watcherThread = nullptr;

void WriteLog(const std::string& msg) {
    auto logPath = phoenix_themes::GetLogPath();
    size_t slash = logPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        phoenix_themes::EnsureDirRecursive(logPath.substr(0, slash));
    }
    HANDLE h = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, msg.data(), static_cast<DWORD>(msg.size()), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    CloseHandle(h);
}

bool ContainsInsensitive(const wchar_t* haystack, const wchar_t* needle) {
    if (!haystack || !needle) return false;
    size_t nlen = wcslen(needle);
    if (nlen == 0) return true;
    for (const wchar_t* p = haystack; *p; ++p) {
        if (_wcsnicmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

bool EndsWithInsensitive(const std::wstring& s, const wchar_t* suffix) {
    size_t slen = wcslen(suffix);
    if (s.size() < slen) return false;
    return _wcsicmp(s.c_str() + (s.size() - slen), suffix) == 0;
}

bool FileExists(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring TrimQuotes(std::wstring s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'"')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'"')) s.pop_back();
    return s;
}

std::wstring GetModulePathDynamic(HMODULE mod) {
    std::wstring buf(512, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(mod, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size() - 1) {
            buf.resize(n);
            return buf;
        }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
}

IUnknown* GetCanonicalIdentity(IUnknown* obj) {
    if (!obj) return nullptr;
    IUnknown* id = nullptr;
    if (SUCCEEDED(obj->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&id))) && id) {
        id->Release();
        return id;
    }
    return obj;
}

// ============================================================================
// DirectWrite Font Engine (System fonts + In-memory local .ttf/.otf)
// ============================================================================

void EnsureCustomFontFileLoadedLocked(IDWriteFactory2* factory2, IUnknown* factoryId) {
    if (g_customFontAttempted && g_customFontFactoryId == factoryId) return;
    if (g_customFontAttempted && g_customFontFactoryId != nullptr && g_customFontFactoryId != factoryId) {
        // Different DWrite factory (e.g. isolated factory) — do not share cross-factory collection
        return;
    }
    g_customFontAttempted = true;
    g_customFontFactoryId = factoryId;

    std::wstring fontFilePath = TrimQuotes(g_cfg.custom_font_file);
    if (!FileExists(fontFilePath)) return;

    IDWriteFactory5* factory5 = nullptr;
    if (FAILED(factory2->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(&factory5))) || !factory5) {
        return;
    }

    HANDLE fh = CreateFileW(fontFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) {
        factory5->Release();
        return;
    }
    LARGE_INTEGER fsize{};
    if (!GetFileSizeEx(fh, &fsize) || fsize.QuadPart <= 0 || fsize.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(fh);
        factory5->Release();
        return;
    }

    // DirectWrite makes an internal copy when ownerObject == nullptr; RAII vector avoids leaking 15-64MB!
    std::vector<BYTE> buffer;
    try {
        buffer.resize(static_cast<size_t>(fsize.QuadPart));
    } catch (...) {
        CloseHandle(fh);
        factory5->Release();
        return;
    }
    DWORD read = 0;
    BOOL ok = ReadFile(fh, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    CloseHandle(fh);
    if (!ok || read != static_cast<DWORD>(buffer.size())) {
        factory5->Release();
        return;
    }

    IDWriteInMemoryFontFileLoader* loader = nullptr;
    IDWriteFontFile* fontFile = nullptr;
    IDWriteFontSetBuilder1* builder = nullptr;
    IDWriteFontSet* fontSet = nullptr;
    IDWriteFontCollection1* collection = nullptr;

    HRESULT hr = factory5->CreateInMemoryFontFileLoader(&loader);
    if (SUCCEEDED(hr) && loader) hr = factory5->RegisterFontFileLoader(loader);
    if (SUCCEEDED(hr) && loader) hr = loader->CreateInMemoryFontFileReference(factory5, buffer.data(), read, nullptr, &fontFile);
    if (SUCCEEDED(hr) && fontFile) hr = factory5->CreateFontSetBuilder(&builder);
    if (SUCCEEDED(hr) && builder) hr = builder->AddFontFile(fontFile);
    if (SUCCEEDED(hr) && builder) hr = builder->CreateFontSet(&fontSet);
    if (SUCCEEDED(hr) && fontSet) hr = factory5->CreateFontCollectionFromFontSet(fontSet, &collection);

    if (SUCCEEDED(hr) && collection && collection->GetFontFamilyCount() > 0) {
        IDWriteFontFamily* fam = nullptr;
        if (SUCCEEDED(collection->GetFontFamily(0, &fam)) && fam) {
            IDWriteLocalizedStrings* names = nullptr;
            if (SUCCEEDED(fam->GetFamilyNames(&names)) && names) {
                UINT32 idx = 0;
                BOOL exists = FALSE;
                if (FAILED(names->FindLocaleName(L"en-us", &idx, &exists)) || !exists) idx = 0;
                UINT32 len = 0;
                if (SUCCEEDED(names->GetStringLength(idx, &len)) && len > 0 && len < 256) {
                    std::wstring familyName(len + 1, L'\0');
                    if (SUCCEEDED(names->GetString(idx, familyName.data(), len + 1))) {
                        familyName.resize(len);
                        g_customFontFamily = std::move(familyName);
                        g_customFontCollection = collection;
                        g_customFontCollection->AddRef();
                        WriteLog("Loaded custom_font_file family=" + phoenix_themes::WideToUtf8(g_customFontFamily));
                    }
                }
                names->Release();
            }
            fam->Release();
        }
    }

    if (collection) collection->Release();
    if (fontSet) fontSet->Release();
    if (builder) builder->Release();
    if (fontFile) fontFile->Release();
    if (loader) loader->Release();
    factory5->Release();
}

IDWriteFontFallback* GetOrCreateCustomFallback(IDWriteFactory2* factory2) {
    IUnknown* factoryId = GetCanonicalIdentity(factory2);
    AcquireSRWLockShared(&g_lock);
    if (g_customFallback && g_customFallbackFactoryId == factoryId) {
        IDWriteFontFallback* cached = g_customFallback;
        cached->AddRef();
        ReleaseSRWLockShared(&g_lock);
        return cached;
    }
    ReleaseSRWLockShared(&g_lock);

    AcquireSRWLockExclusive(&g_lock);
    if (g_customFallback && g_customFallbackFactoryId == factoryId) {
        g_customFallback->AddRef();
        IDWriteFontFallback* res = g_customFallback;
        ReleaseSRWLockExclusive(&g_lock);
        return res;
    }

    EnsureCustomFontFileLoadedLocked(factory2, factoryId);
    const bool hasCustomFile = (g_customFontCollection != nullptr &&
                                !g_customFontFamily.empty() &&
                                g_customFontFactoryId == factoryId);
    const bool hasCjkName = !g_cfg.cjk_font.empty();
    if (!hasCustomFile && !hasCjkName) {
        ReleaseSRWLockExclusive(&g_lock);
        return nullptr;
    }

    IDWriteFontFallback* origFallback = nullptr;
    HRESULT hr = g_origGetSystemFontFallback ? g_origGetSystemFontFallback(factory2, &origFallback) : E_FAIL;
    if (FAILED(hr) || !origFallback) {
        ReleaseSRWLockExclusive(&g_lock);
        return nullptr;
    }

    IDWriteFontFallbackBuilder* builder = nullptr;
    hr = factory2->CreateFontFallbackBuilder(&builder);
    if (FAILED(hr) || !builder) {
        origFallback->Release();
        ReleaseSRWLockExclusive(&g_lock);
        return nullptr;
    }

    static constexpr DWRITE_UNICODE_RANGE kCjkRanges[] = {
        { 0x2E80, 0x2FDF }, { 0x3000, 0x303F }, { 0x3040, 0x309F },
        { 0x30A0, 0x30FF }, { 0x31F0, 0x31FF }, { 0x3200, 0x33FF },
        { 0x3400, 0x4DBF }, { 0x4E00, 0x9FFF }, { 0xF900, 0xFAFF },
        { 0xFF00, 0xFFEF }, { 0x20000, 0x2FA1F },
    };
    const UINT32 rangeCount = static_cast<UINT32>(sizeof(kCjkRanges) / sizeof(kCjkRanges[0]));

    if (hasCustomFile) {
        const WCHAR* customFamilies[] = { g_customFontFamily.c_str() };
        builder->AddMapping(kCjkRanges, rangeCount, customFamilies, 1,
                            g_customFontCollection, nullptr, nullptr, g_cfg.font_scale);
    }
    if (hasCjkName) {
        const WCHAR* cjkFamilies[] = { g_cfg.cjk_font.c_str() };
        builder->AddMapping(kCjkRanges, rangeCount, cjkFamilies, 1,
                            nullptr, nullptr, nullptr, g_cfg.font_scale);
    }
    builder->AddMappings(origFallback);

    IDWriteFontFallback* created = nullptr;
    HRESULT hr3 = builder->CreateFontFallback(&created);
    builder->Release();
    origFallback->Release();

    if (SUCCEEDED(hr3) && created) {
        if (!g_customFallback) {
            g_customFallback = created;
            g_customFallbackFactoryId = factoryId;
            g_customFallback->AddRef();
        }
        ReleaseSRWLockExclusive(&g_lock);
        return created;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return nullptr;
}

HRESULT STDMETHODCALLTYPE Hooked_GetSystemFontFallback(IDWriteFactory2* self, IDWriteFontFallback** fontFallback) {
    if (!fontFallback) return E_INVALIDARG;
    *fontFallback = nullptr;
    if (g_cfg.font_enabled) {
        IDWriteFontFallback* custom = GetOrCreateCustomFallback(self);
        if (custom) {
            *fontFallback = custom;
            return S_OK;
        }
    }
    return g_origGetSystemFontFallback ? g_origGetSystemFontFallback(self, fontFallback) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE Hooked_CreateTextFormat(
    IDWriteFactory* self, WCHAR const* fontFamilyName, IDWriteFontCollection* fontCollection,
    DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, DWRITE_FONT_STRETCH fontStretch,
    FLOAT fontSize, WCHAR const* localeName, IDWriteTextFormat** textFormat) {

    WCHAR const* effectiveFamily = fontFamilyName;
    IDWriteFontCollection* effectiveCollection = fontCollection;
    FLOAT effectiveSize = fontSize;
    std::wstring localCustomFamily;
    IDWriteFontCollection1* localCustomCollection = nullptr;

    IDWriteFactory2* factory2 = nullptr;
    if (g_cfg.font_enabled) {
        self->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&factory2));
    }

    if (g_cfg.font_enabled && g_cfg.font_mode == L"all" && fontFamilyName) {
        IUnknown* factoryId = GetCanonicalIdentity(self);
        if (factory2) {
            AcquireSRWLockExclusive(&g_lock);
            EnsureCustomFontFileLoadedLocked(factory2, factoryId);
            if (g_customFontCollection && !g_customFontFamily.empty() && g_customFontFactoryId == factoryId) {
                localCustomFamily = g_customFontFamily;
                localCustomCollection = g_customFontCollection;
                localCustomCollection->AddRef();
            }
            ReleaseSRWLockExclusive(&g_lock);
        }
        bool isSpecial = ContainsInsensitive(fontFamilyName, L"Emoji") ||
                         ContainsInsensitive(fontFamilyName, L"Symbol") ||
                         ContainsInsensitive(fontFamilyName, L"Icon");
        if (!isSpecial) {
            bool isMono = ContainsInsensitive(fontFamilyName, L"mono") ||
                          ContainsInsensitive(fontFamilyName, L"Consolas") ||
                          ContainsInsensitive(fontFamilyName, L"Code");
            if (isMono && !g_cfg.mono_font.empty()) {
                effectiveFamily = g_cfg.mono_font.c_str();
                effectiveCollection = nullptr;
                effectiveSize = fontSize * g_cfg.font_scale;
            } else if (localCustomCollection && !localCustomFamily.empty() && g_cfg.latin_font.empty()) {
                effectiveFamily = localCustomFamily.c_str();
                effectiveCollection = localCustomCollection;
                effectiveSize = fontSize * g_cfg.font_scale;
            } else {
                const std::wstring& target = !g_cfg.latin_font.empty() ? g_cfg.latin_font : g_cfg.cjk_font;
                if (!target.empty()) {
                    effectiveFamily = target.c_str();
                    effectiveCollection = nullptr;
                    effectiveSize = fontSize * g_cfg.font_scale;
                }
            }
        }
    }

    HRESULT hr = g_origCreateTextFormat
        ? g_origCreateTextFormat(self, effectiveFamily, effectiveCollection, fontWeight, fontStyle, fontStretch, effectiveSize, localeName, textFormat)
        : E_FAIL;
    if (FAILED(hr) && g_origCreateTextFormat && (effectiveFamily != fontFamilyName || effectiveCollection != fontCollection)) {
        hr = g_origCreateTextFormat(
            self, fontFamilyName, fontCollection, fontWeight, fontStyle, fontStretch, fontSize, localeName, textFormat);
    }

    if (localCustomCollection) localCustomCollection->Release();

    if (g_cfg.font_enabled && SUCCEEDED(hr) && textFormat && *textFormat && factory2) {
        IDWriteFontFallback* custom = GetOrCreateCustomFallback(factory2);
        if (custom) {
            IDWriteTextFormat1* tf1 = nullptr;
            if (SUCCEEDED((*textFormat)->QueryInterface(__uuidof(IDWriteTextFormat1), reinterpret_cast<void**>(&tf1))) && tf1) {
                tf1->SetFontFallback(custom);
                tf1->Release();
                if (InterlockedCompareExchange(&g_loggedFirstTextFormat, 1, 0) == 0) {
                    WriteLog("Applied font hook (mode=" + phoenix_themes::WideToUtf8(g_cfg.font_mode) +
                             ", cjk=" + phoenix_themes::WideToUtf8(g_cfg.cjk_font) + ")");
                }
            }
            custom->Release();
        }
    }

    if (factory2) factory2->Release();
    return hr;
}

void HookFactoryVtable(IUnknown* unk) {
    if (!unk) return;
    IDWriteFactory2* factory2 = nullptr;
    if (FAILED(unk->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&factory2))) || !factory2) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (!g_vtableHooked) {
        void** vtable = *reinterpret_cast<void***>(factory2);
        DWORD oldProt = 0;
        const size_t spanBytes = sizeof(void*) * (kIdxGetSystemFontFallback - kIdxCreateTextFormat + 1);
        if (VirtualProtect(&vtable[kIdxCreateTextFormat], spanBytes, PAGE_READWRITE, &oldProt)) {
            g_origCreateTextFormat = reinterpret_cast<PFN_CreateTextFormat>(vtable[kIdxCreateTextFormat]);
            g_origGetSystemFontFallback = reinterpret_cast<PFN_GetSystemFontFallback>(vtable[kIdxGetSystemFontFallback]);
            vtable[kIdxCreateTextFormat] = reinterpret_cast<void*>(&Hooked_CreateTextFormat);
            vtable[kIdxGetSystemFontFallback] = reinterpret_cast<void*>(&Hooked_GetSystemFontFallback);
            DWORD tmp = 0;
            VirtualProtect(&vtable[kIdxCreateTextFormat], spanBytes, oldProt, &tmp);
            g_vtableHooked = true;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    factory2->Release();
}

HRESULT WINAPI Hooked_DWriteCreateFactory(DWRITE_FACTORY_TYPE factoryType, REFIID iid, IUnknown** factory) {
    if (!g_origDWriteCreateFactory) return E_FAIL;
    HRESULT hr = g_origDWriteCreateFactory(factoryType, iid, factory);
    if (SUCCEEDED(hr) && factory && *factory) HookFactoryVtable(*factory);
    return hr;
}

// ============================================================================
// Ctrl+R / Self-Relaunch Auto Re-Injection
// ============================================================================

bool QueueLoadDll(HANDLE hProcess, HANDLE hThread, const std::wstring& dllPath) {
    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;
    if (!WriteProcessMemory(hProcess, remote, dllPath.c_str(), bytes, nullptr)) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return false;
    }
    auto loadLib = reinterpret_cast<PAPCFUNC>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadLib) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return false;
    }
    if (QueueUserAPC(loadLib, hThread, reinterpret_cast<ULONG_PTR>(remote)) == 0) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

bool MatchesTargetDiscordExe(const std::wstring& exeCandidate) {
    if (exeCandidate.empty() || g_selfExePath.empty()) return false;
    if (_wcsicmp(exeCandidate.c_str(), g_selfExePath.c_str()) == 0) return true;
    return EndsWithInsensitive(exeCandidate, L"\\DiscordPhoenix\\app\\Discord.exe");
}

bool IsTargetSelfRelaunch(LPCWSTR appName, LPCWSTR cmdLine) {
    if (g_selfExePath.empty()) return false;
    // If lpApplicationName is explicitly given, judge ONLY by lpApplicationName (never scan args!)
    if (appName && appName[0] != L'\0') {
        return MatchesTargetDiscordExe(appName);
    }
    if (!cmdLine || cmdLine[0] == L'\0') return false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    bool match = false;
    if (argv && argc > 0 && argv[0]) {
        match = MatchesTargetDiscordExe(argv[0]);
    }
    if (argv) LocalFree(argv);
    return match;
}

BOOL WINAPI Hooked_CreateProcessW(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
    LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation) {

    auto cfg = phoenix_themes::LoadConfig();
    if (!cfg.keep_on_relaunch || !IsTargetSelfRelaunch(lpApplicationName, lpCommandLine)) {
        return g_origCreateProcessW(
            lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
            bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory,
            lpStartupInfo, lpProcessInformation);
    }

    bool callerWantedSuspended = (dwCreationFlags & CREATE_SUSPENDED) != 0;
    BOOL ok = g_origCreateProcessW(
        lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
        bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment, lpCurrentDirectory,
        lpStartupInfo, lpProcessInformation);

    if (ok && lpProcessInformation && lpProcessInformation->hProcess && lpProcessInformation->hThread) {
        bool injectedThemes = false;
        if (!g_selfDllPath.empty() && FileExists(g_selfDllPath)) {
            injectedThemes = QueueLoadDll(lpProcessInformation->hProcess, lpProcessInformation->hThread, g_selfDllPath);
        }
        bool noSunbirdEnv = !phoenix_themes::GetEnvVar(L"PHOENIX_THEMES_NO_SUNBIRD").empty();
        std::wstring sunbirdDll = phoenix_themes::GetSunbirdBinDir() + L"\\phoenix.dll";
        if (cfg.use_sunbird && !noSunbirdEnv && FileExists(sunbirdDll)) {
            QueueLoadDll(lpProcessInformation->hProcess, lpProcessInformation->hThread, sunbirdDll);
        }
        if (injectedThemes) {
            WriteLog("Re-injected Phoenix Themes into relaunched Discord.exe");
        }
        if (!callerWantedSuspended) ResumeThread(lpProcessInformation->hThread);
    }
    return ok;
}

// ============================================================================
// Live Window Appearance + Theme Hot-Reload + Native Dialogs
// ============================================================================

struct ApplyWindowCtx {
    DWORD pid;
    phoenix_themes::Config cfg;
    HWND primaryHwnd;
    int appliedCount;
};

BOOL CALLBACK ApplyToProcessWindow(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<ApplyWindowCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc) || (rc.right - rc.left) < 200 || (rc.bottom - rc.top) < 200) {
        return TRUE;
    }

    if (!ctx->primaryHwnd) ctx->primaryHwnd = hwnd;

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ctx->cfg.window_opacity < 100) {
        if (!(ex & WS_EX_LAYERED)) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            g_layeredHwnds.insert(hwnd);
        }
        BYTE alpha = static_cast<BYTE>((ctx->cfg.window_opacity * 255) / 100);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    } else if (g_layeredHwnds.count(hwnd) != 0) {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        if (ex & WS_EX_LAYERED) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        g_layeredHwnds.erase(hwnd);
    }

    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode, sizeof(darkMode));

    DWORD cornerVal = 0;
    if (ctx->cfg.corner_style == L"square") cornerVal = 1;
    else if (ctx->cfg.corner_style == L"round") cornerVal = 2;
    else if (ctx->cfg.corner_style == L"round_small") cornerVal = 3;
    DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &cornerVal, sizeof(cornerVal));

    COLORREF borderCol = kDwmColorDefault;
    if (_wcsicmp(ctx->cfg.border_color.c_str(), L"none") == 0) {
        borderCol = kDwmColorNone;
    } else if (!ctx->cfg.border_color.empty()) {
        phoenix_themes::ParseHexColorRef(ctx->cfg.border_color, borderCol);
    }
    DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &borderCol, sizeof(borderCol));

    COLORREF captionCol = kDwmColorDefault;
    if (!ctx->cfg.caption_color.empty()) {
        phoenix_themes::ParseHexColorRef(ctx->cfg.caption_color, captionCol);
    }
    DwmSetWindowAttribute(hwnd, kDwmwaCaptionColor, &captionCol, sizeof(captionCol));

    bool isTopmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (isTopmost != ctx->cfg.always_on_top) {
        SetWindowPos(hwnd, ctx->cfg.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if (InterlockedCompareExchange(&g_loggedFirstWindow, 1, 0) == 0) {
        WriteLog("Applied live window tweaks to Discord Phoenix window");
    }

    ctx->appliedCount++;
    return TRUE;
}

template <typename Fn>
void ModifyOverridesLocked(Fn&& fn) {
    AcquireSRWLockExclusive(&g_fileLock);
    auto ov = phoenix_themes::LoadOverrides();
    fn(ov);
    phoenix_themes::SaveOverrides(ov);
    ReleaseSRWLockExclusive(&g_fileLock);
}

template <typename Fn>
void ModifyPrimaryConfigIfExistsLocked(Fn&& fn) {
    AcquireSRWLockExclusive(&g_fileLock);
    std::wstring cfgPath = phoenix_themes::GetConfigPath();
    if (FileExists(cfgPath)) {
        auto cfg = phoenix_themes::LoadConfig(cfgPath, L"", L"");
        fn(cfg);
        phoenix_themes::SaveConfig(cfg, cfgPath);
    }
    ReleaseSRWLockExclusive(&g_fileLock);
}

struct DialogTask {
    std::wstring action;
};

DWORD WINAPI DialogWorkerProc(LPVOID param) {
    auto* task = reinterpret_cast<DialogTask*>(param);
    std::wstring action = task->action;
    delete task;

    // STA COM initialization is required for modern GetOpenFileNameW shell dialogs and ShellExecuteW!
    // Never pass cross-thread Discord HWND as hwndOwner (avoids Win32 AttachThreadInput deadlocks).
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool shouldUninit = SUCCEEDED(hrCom);

    if (action == L"pick_wallpaper") {
        std::vector<wchar_t> fileBuf(4096, L'\0');
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg;*.webp;*.bmp)\0*.png;*.jpg;*.jpeg;*.webp;*.bmp\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = fileBuf.data();
        ofn.nMaxFile = static_cast<DWORD>(fileBuf.size());
        ofn.lpstrTitle = L"Phoenix Themes - Select Background Wallpaper Image";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) {
            std::wstring picked = fileBuf.data();
            ModifyOverridesLocked([&](phoenix_themes::Overrides& ov) { ov.wallpaper_file = picked; });
            WriteLog("Selected wallpaper image: " + phoenix_themes::WideToUtf8(picked));
        }
    } else if (action == L"pick_font") {
        std::vector<wchar_t> fileBuf(4096, L'\0');
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = L"Font Files (*.ttf;*.otf)\0*.ttf;*.otf\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = fileBuf.data();
        ofn.nMaxFile = static_cast<DWORD>(fileBuf.size());
        ofn.lpstrTitle = L"Phoenix Themes - Select .ttf / .otf Font File";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) {
            std::wstring picked = fileBuf.data();
            ModifyOverridesLocked([&](phoenix_themes::Overrides& ov) { ov.custom_font_file = picked; });
            WriteLog("Selected custom font via dialog: " + phoenix_themes::WideToUtf8(picked));
            std::wstring msg = L"Custom font file saved:\n" + picked +
                               L"\n\nPress Ctrl+R in Discord Phoenix to apply it.";
            MessageBoxW(nullptr, msg.c_str(), L"Phoenix Themes", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        }
    } else if (action == L"pick_border_color") {
        static COLORREF customPalette[16] = {
            RGB(0x7A, 0xA2, 0xF7), RGB(0xCB, 0xA6, 0xF7), RGB(0x34, 0xD3, 0x99), RGB(0xFF, 0x79, 0xC6),
            RGB(0x58, 0x65, 0xF2), RGB(0x88, 0xC0, 0xD0), RGB(0xFA, 0xBD, 0x2F), RGB(0xEB, 0x6F, 0x92),
            RGB(0xFF, 0xFF, 0xFF), RGB(0x1E, 0x1F, 0x22), RGB(0x00, 0xD4, 0xFF), RGB(0xBD, 0x93, 0xF9),
            RGB(0x50, 0xFA, 0x7B), RGB(0xFF, 0xB8, 0x6C), RGB(0xF3, 0x8B, 0xA8), RGB(0x00, 0x00, 0x00),
        };
        AcquireSRWLockShared(&g_fileLock);
        auto ov = phoenix_themes::LoadOverrides();
        ReleaseSRWLockShared(&g_fileLock);
        COLORREF initial = RGB(0x7A, 0xA2, 0xF7);
        if (!ov.border_color.empty()) phoenix_themes::ParseHexColorRef(ov.border_color, initial);
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = nullptr;
        cc.rgbResult = initial;
        cc.lpCustColors = customPalette;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) {
            std::wstring hex = phoenix_themes::FormatColorRefHex(cc.rgbResult);
            ModifyOverridesLocked([&](phoenix_themes::Overrides& o) { o.border_color = hex; });
        }
    } else if (action == L"open_config") {
        std::wstring ovPath = phoenix_themes::GetOverridesPath();
        AcquireSRWLockExclusive(&g_fileLock);
        if (!FileExists(ovPath)) phoenix_themes::SaveOverrides(phoenix_themes::LoadOverrides(ovPath), ovPath);
        ReleaseSRWLockExclusive(&g_fileLock);
        HINSTANCE res = ShellExecuteW(nullptr, L"open", L"notepad.exe",
                                      (L"\"" + ovPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(res) <= 32) {
            ShellExecuteW(nullptr, L"open", ovPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } else if (action == L"open_themes_dir") {
        std::wstring dir = phoenix_themes::GetThemesDir();
        phoenix_themes::EnsureDirRecursive(dir);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (action == L"edit_active_theme") {
        auto cur = phoenix_themes::LoadConfig();
        std::wstring themeFile = cur.active_theme.empty() ? L"custom.theme.css" : cur.active_theme;
        std::wstring fullPath = phoenix_themes::ResolveThemeFilePath(themeFile);
        if (!FileExists(fullPath)) {
            static const char* kTemplate =
                "/* @name Custom Theme\n"
                "   Edit variables below and press Ctrl+S to hot-reload live inside Discord Phoenix! */\n\n"
                ":root {\n"
                "  --bg-tertiary: #16161e;\n"
                "  --bg-secondary: #1a1b26;\n"
                "  --bg-primary: #1f2335;\n"
                "  --bg-elevated: #292e42;\n\n"
                "  --accent-color: #7aa2f7;\n"
                "  --ui-border-color: #3b4261;\n\n"
                "  --text-normal: #c0caf5;\n"
                "  --text-muted: #737aa2;\n\n"
                "  --radius-scale: 1.0;\n"
                "  --override-nitro: true;\n\n"
                "  /* Optional background wallpaper (PNG / JPG / WebP):\n"
                "  --background-image: url(\"./wallpaper.png\");\n"
                "  --background-opacity: 0.35;\n"
                "  --panel-opacity: 0.78;\n"
                "  */\n"
                "}\n";
            phoenix_themes::WriteTextFile(fullPath, kTemplate);
        }
        HINSTANCE res = ShellExecuteW(nullptr, L"open", L"notepad.exe",
                                      (L"\"" + fullPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(res) <= 32) {
            ShellExecuteW(nullptr, L"open", fullPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    if (shouldUninit) CoUninitialize();
    InterlockedExchange(&g_dialogBusy, 0);
    return 0;
}

void HandleUiAction(const std::wstring& rawAction) {
    if (rawAction.empty()) return;
    size_t colon = rawAction.find(L':');
    std::wstring cmd = (colon == std::wstring::npos) ? rawAction : rawAction.substr(0, colon);

    if (cmd == L"clear_wallpaper") {
        ModifyOverridesLocked([](phoenix_themes::Overrides& ov) { ov.wallpaper_file.clear(); });
        ModifyPrimaryConfigIfExistsLocked([](phoenix_themes::Config& c) { c.wallpaper_file.clear(); });
        WriteLog("Cleared wallpaper override");
        return;
    }
    if (cmd == L"clear_font") {
        ModifyOverridesLocked([](phoenix_themes::Overrides& ov) { ov.custom_font_file.clear(); });
        ModifyPrimaryConfigIfExistsLocked([](phoenix_themes::Config& c) { c.custom_font_file.clear(); });
        return;
    }
    if (cmd == L"clear_border_override") {
        ModifyOverridesLocked([](phoenix_themes::Overrides& ov) { ov.border_color.clear(); });
        return;
    }
    if (cmd == L"reset_window") {
        ModifyOverridesLocked([](phoenix_themes::Overrides& ov) {
            ov.border_color.clear();
            ov.caption_color.clear();
            ov.wallpaper_file.clear();
        });
        ModifyPrimaryConfigIfExistsLocked([](phoenix_themes::Config& c) {
            c.window_opacity = 100;
            c.corner_style = L"default";
            c.border_color.clear();
            c.caption_color.clear();
            c.always_on_top = false;
            c.wallpaper_file.clear();
        });
        return;
    }

    if (cmd == L"pick_wallpaper" || cmd == L"pick_font" || cmd == L"pick_border_color" ||
        cmd == L"open_config" || cmd == L"open_themes_dir" || cmd == L"edit_active_theme") {
        if (InterlockedCompareExchange(&g_dialogBusy, 1, 0) == 0) {
            auto* task = new (std::nothrow) DialogTask{cmd};
            if (task) {
                HANDLE t = CreateThread(nullptr, 0, DialogWorkerProc, task, 0, nullptr);
                if (t) CloseHandle(t);
                else { delete task; InterlockedExchange(&g_dialogBusy, 0); }
            } else {
                InterlockedExchange(&g_dialogBusy, 0);
            }
        }
    }
}

std::wstring g_loadedWallpaperPath;
ULONGLONG g_loadedWallpaperStamp = 0;

ULONGLONG GetFileStamp(const std::wstring& path) {
    if (path.empty()) return 0;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    ULARGE_INTEGER ul{};
    ul.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    ul.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return ul.QuadPart ^ (static_cast<ULONGLONG>(fad.nFileSizeLow) << 40);
}

void ApplyThemeAndWallpaper(const phoenix_themes::Config& cfg) {
    phoenix_theme::Theme parsed;
    std::wstring themeFullPath;
    if (!cfg.active_theme.empty()) {
        themeFullPath = phoenix_themes::ResolveThemeFilePath(cfg.active_theme);
        std::string css;
        if (phoenix_themes::ReadTextFile(themeFullPath, css)) {
            parsed = phoenix_theme::ParseThemeCss(css);
            WriteLog("Loaded CSS theme: " + phoenix_themes::WideToUtf8(cfg.active_theme) +
                     " (active=" + (parsed.active ? "true" : "false") + ", name=" + parsed.name + ")");
        } else {
            WriteLog("CSS theme file not found: " + phoenix_themes::WideToUtf8(themeFullPath));
        }
    } else {
        WriteLog("CSS theme disabled (Discord native palette)");
    }

    // Config overrides: override_nitro, panel_opacity, wallpaper_opacity
    parsed.override_nitro = cfg.override_nitro && parsed.override_nitro;
    if (!parsed.has_panel_opacity) parsed.panel_opacity = cfg.panel_opacity;
    float wpOpacity = parsed.has_background_opacity ? parsed.background_opacity : cfg.wallpaper_opacity;

    // Resolve wallpaper: explicit config/dialog pick wins, otherwise CSS --background-image
    std::wstring wpPath = TrimQuotes(cfg.wallpaper_file);
    if (wpPath.empty() && !parsed.background_image.empty()) {
        wpPath = phoenix_themes::ResolveRelativePath(themeFullPath, phoenix_themes::Utf8ToWide(parsed.background_image));
    }

    ULONGLONG wpStamp = GetFileStamp(wpPath);
    if (wpPath != g_loadedWallpaperPath || wpStamp != g_loadedWallpaperStamp) {
        g_loadedWallpaperPath = wpPath;
        g_loadedWallpaperStamp = wpStamp;
        if (wpPath.empty()) {
            phoenix_wallpaper::LoadWallpaperFromFile(L"");
        } else if (phoenix_wallpaper::LoadWallpaperFromFile(wpPath)) {
            WriteLog("Loaded wallpaper image: " + phoenix_themes::WideToUtf8(wpPath));
        } else {
            WriteLog("Failed to load wallpaper image: " + phoenix_themes::WideToUtf8(wpPath));
            phoenix_wallpaper::LoadWallpaperFromFile(L"");
        }
    }

    phoenix_d3d::SetActiveTheme(parsed, wpOpacity);
}

DWORD WINAPI WindowWatcherProc(LPVOID) {
    const std::wstring cfgPath = phoenix_themes::GetConfigPath();
    const std::wstring adapterPath = phoenix_themes::GetSunbirdAdapterConfigPath();
    const std::wstring ovPath = phoenix_themes::GetOverridesPath();
    const DWORD pid = GetCurrentProcessId();

    ULONGLONG lastStamp = 0;
    ULONGLONG lastThemeStamp = 0;
    std::wstring lastThemeName;
    bool appliedThemeOnce = false;
    bool appliedWindowOnce = false;
    bool actionInitialized = false;
    std::wstring lastAction;

    while (InterlockedCompareExchange(&g_stopWatcher, 0, 0) == 0) {
        ULONGLONG stamp = GetFileStamp(cfgPath) ^
                          (GetFileStamp(adapterPath) * 2654435761ULL) ^
                          (GetFileStamp(ovPath) * 1315423911ULL);
        ULONGLONG themeStamp = lastThemeName.empty() ? 0 : GetFileStamp(phoenix_themes::ResolveThemeFilePath(lastThemeName));
        bool configChanged = (!appliedThemeOnce || (stamp != 0 && stamp != lastStamp) || themeStamp != lastThemeStamp);

        if (configChanged || !appliedWindowOnce) {
            // Read base config (WITHOUT overrides.json!) first so overrides never bake into config.json
            phoenix_themes::Config baseCfg = phoenix_themes::LoadConfig(cfgPath, adapterPath, L"");

            if (!actionInitialized) {
                lastAction = baseCfg.action;
                actionInitialized = true;
            }

            if (configChanged) {
                // 1. Handle UI action FIRST (so clear_wallpaper / clear_border_override / reset_window run BEFORE mirroring!)
                if (!baseCfg.action.empty() && baseCfg.action != lastAction) {
                    lastAction = baseCfg.action;
                    HandleUiAction(baseCfg.action);
                    baseCfg = phoenix_themes::LoadConfig(cfgPath, adapterPath, L"");
                }

                // 2. Mirror Sunbird adapter updates into canonical config.json WITHOUT overrides.json layered in!
                if (phoenix_themes::GetFileWriteTime(adapterPath) > phoenix_themes::GetFileWriteTime(cfgPath)) {
                    AcquireSRWLockExclusive(&g_fileLock);
                    phoenix_themes::SaveConfig(baseCfg, cfgPath);
                    ReleaseSRWLockExclusive(&g_fileLock);
                }

                // 3. Now load final layered config (with overrides.json on top) and apply!
                phoenix_themes::Config liveCfg = phoenix_themes::LoadConfig(cfgPath, adapterPath, ovPath);
                std::wstring newThemePath = phoenix_themes::ResolveThemeFilePath(liveCfg.active_theme);
                ULONGLONG curThemeStamp = GetFileStamp(newThemePath);
                lastThemeName = liveCfg.active_theme;
                lastThemeStamp = curThemeStamp;
                ApplyThemeAndWallpaper(liveCfg);
                appliedThemeOnce = true;

                ApplyWindowCtx ctx{pid, liveCfg, nullptr, 0};
                EnumWindows(ApplyToProcessWindow, reinterpret_cast<LPARAM>(&ctx));
                if (ctx.primaryHwnd) {
                    InvalidateRect(ctx.primaryHwnd, nullptr, FALSE);
                    PostMessageW(ctx.primaryHwnd, WM_PAINT, 0, 0);
                }
                if (ctx.appliedCount > 0) appliedWindowOnce = true;

                lastStamp = GetFileStamp(cfgPath) ^
                            (GetFileStamp(adapterPath) * 2654435761ULL) ^
                            (GetFileStamp(ovPath) * 1315423911ULL);
            } else {
                phoenix_themes::Config liveCfg = phoenix_themes::LoadConfig(cfgPath, adapterPath, ovPath);
                ApplyWindowCtx ctx{pid, liveCfg, nullptr, 0};
                EnumWindows(ApplyToProcessWindow, reinterpret_cast<LPARAM>(&ctx));
                if (ctx.appliedCount > 0) appliedWindowOnce = true;
            }
        }
        Sleep(350);
    }
    return 0;
}

// ============================================================================
// Generic IAT Patch Helper
// ============================================================================

bool PatchIatFunction(HMODULE module, const char* targetDll, const char* targetFunc, void* hookFn, void** origOut) {
    if (!module) return false;
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name != 0; ++desc) {
        const char* dllName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dllName, targetDll) != 0) continue;

        auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

        for (; origThunk->u1.AddressOfData != 0; ++origThunk, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;
            auto* imp = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(imp->Name), targetFunc) == 0) {
                DWORD oldProt = 0;
                if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProt)) {
                    *origOut = reinterpret_cast<void*>(thunk->u1.Function);
                    thunk->u1.Function = reinterpret_cast<ULONG_PTR>(hookFn);
                    DWORD tmp = 0;
                    VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProt, &tmp);
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = hinst;
        DisableThreadLibraryCalls(hinst);

        // Pin module in process so dynamic FreeLibrary can never unmap active hooks or watcher threads
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);

        g_selfDllPath = GetModulePathDynamic(hinst);
        g_selfExePath = GetModulePathDynamic(nullptr);

        g_cfg = phoenix_themes::LoadConfig();
        HMODULE mainMod = GetModuleHandleW(nullptr);

        if (g_cfg.font_enabled) {
            if (PatchIatFunction(mainMod, "dwrite.dll", "DWriteCreateFactory",
                                 reinterpret_cast<void*>(&Hooked_DWriteCreateFactory),
                                 reinterpret_cast<void**>(&g_origDWriteCreateFactory))) {
                WriteLog("Patched IAT: dwrite.dll!DWriteCreateFactory");
            }
        }

        if (PatchIatFunction(mainMod, "kernel32.dll", "CreateProcessW",
                             reinterpret_cast<void*>(&Hooked_CreateProcessW),
                             reinterpret_cast<void**>(&g_origCreateProcessW))) {
            WriteLog("Patched IAT: kernel32.dll!CreateProcessW (relaunch guard active)");
        }

        phoenix_d3d::SetLogCallback(&WriteLog);
        if (PatchIatFunction(mainMod, "d3d11.dll", "D3D11CreateDevice",
                             phoenix_d3d::GetCreateDeviceHookPtr(),
                             phoenix_d3d::GetCreateDeviceOrigSlot())) {
            WriteLog("Patched IAT: d3d11.dll!D3D11CreateDevice (Phoenix Themes GPUI engine active)");
        }
        PatchIatFunction(mainMod, "dxgi.dll", "CreateDXGIFactory1",
                         phoenix_d3d::GetCreateDxgiFactory1HookPtr(),
                         phoenix_d3d::GetCreateDxgiFactory1OrigSlot());
        PatchIatFunction(mainMod, "dxgi.dll", "CreateDXGIFactory2",
                         phoenix_d3d::GetCreateDxgiFactory2HookPtr(),
                         phoenix_d3d::GetCreateDxgiFactory2OrigSlot());

        if (phoenix_themes::GetEnvVar(L"PHOENIX_THEMES_NO_WATCHER").empty()) {
            g_watcherThread = CreateThread(nullptr, 0, WindowWatcherProc, nullptr, 0, nullptr);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stopWatcher, 1);
        if (g_watcherThread) {
            CloseHandle(g_watcherThread);
            g_watcherThread = nullptr;
        }
    }
    return TRUE;
}
