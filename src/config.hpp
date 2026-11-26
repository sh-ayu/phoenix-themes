#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace phoenix_themes {

struct Config {
    // Active .theme.css filename in GetThemesDir() (or full path), "" = Discord native
    std::wstring active_theme = L"";
    // Optional wallpaper image override (PNG/JPG/WebP/BMP) — overrides or supplements theme's --background-image
    std::wstring wallpaper_file = L"";
    float wallpaper_opacity = 0.35f; // 0.05..1.0
    float panel_opacity = 0.78f;     // 0.25..1.0 (used when wallpaper is active)
    bool override_nitro = true;      // Eliminate Nitro gradient bleed-through

    // Font settings
    bool font_enabled = true;
    std::wstring font_mode = L"cjk_fallback"; // "cjk_fallback" | "all"
    std::wstring cjk_font = L"Meiryo";
    std::wstring latin_font = L"";
    std::wstring mono_font = L"";
    std::wstring custom_font_file = L"";
    float font_scale = 1.0f;

    // Live window appearance (applied without restart)
    int window_opacity = 100;               // 50..100
    std::wstring corner_style = L"default"; // "default" | "round" | "round_small" | "square"
    std::wstring border_color = L"";        // "#RRGGBB", "none", or ""
    std::wstring caption_color = L"";       // "#RRGGBB" or ""
    bool always_on_top = false;

    // Launcher / loader interop (loader-independent: Sunbird is optional!)
    bool keep_on_relaunch = true;
    bool use_sunbird = true;
    bool auto_update_sunbird = true;

    // One-shot UI button trigger ("<action>:<timestamp>")
    std::wstring action = L"";
};

// User/dialog overrides stored separately so mod-loader UI adapters never clobber native dialog picks.
struct Overrides {
    std::wstring cjk_font = L"";
    std::wstring latin_font = L"";
    std::wstring mono_font = L"";
    std::wstring custom_font_file = L"";
    std::wstring border_color = L"";
    std::wstring caption_color = L"";
    std::wstring wallpaper_file = L"";
};

inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

inline std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

inline std::wstring GetEnvVar(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0 || needed > 32768) return {};
    std::wstring buf(needed, L'\0');
    DWORD n = GetEnvironmentVariableW(name, buf.data(), needed);
    if (n == 0 || n >= needed) return {};
    buf.resize(n);
    return buf;
}

inline std::wstring GetLocalAppData() { return GetEnvVar(L"LOCALAPPDATA"); }
inline std::wstring GetRoamingAppData() { return GetEnvVar(L"APPDATA"); }

// Canonical loader-independent data directory: %APPDATA%\phoenix-themes
inline std::wstring GetBaseDir() {
    auto envHome = GetEnvVar(L"PHOENIX_THEMES_HOME");
    if (!envHome.empty()) return envHome;
    auto appData = GetRoamingAppData();
    if (!appData.empty()) return appData + L"\\phoenix-themes";
    auto local = GetLocalAppData();
    return local.empty() ? L".\\phoenix-themes" : (local + L"\\phoenix-themes");
}

inline std::wstring GetThemesDir() {
    auto envDir = GetEnvVar(L"PHOENIX_THEMES_DIR");
    if (!envDir.empty()) return envDir;
    return GetBaseDir() + L"\\themes";
}

inline std::wstring GetConfigPath() {
    return GetBaseDir() + L"\\config.json";
}

inline std::wstring GetOverridesPath() {
    return GetBaseDir() + L"\\overrides.json";
}

inline std::wstring GetLogPath() {
    return GetBaseDir() + L"\\phoenix-themes.log";
}

// Optional Sunbird adapter locations (used when Sunbird interop is enabled)
inline std::wstring GetSunbirdBinDir() {
    auto local = GetLocalAppData();
    return local.empty() ? L"." : (local + L"\\Sunbird");
}

inline std::wstring GetSunbirdHomeDir() {
    auto envHome = GetEnvVar(L"PHOENIX_HOME");
    if (!envHome.empty()) return envHome;
    auto local = GetLocalAppData();
    return local.empty() ? L"." : (local + L"\\DiscordPhoenix\\sunbird");
}

inline std::wstring GetSunbirdAdapterConfigPath() {
    return GetSunbirdHomeDir() + L"\\storage\\phoenix-themes.json";
}

inline std::wstring GetSunbirdAdapterPluginPath() {
    return GetSunbirdHomeDir() + L"\\plugins\\phoenix-themes.js";
}

inline std::wstring ResolveThemeFilePath(const std::wstring& themeName) {
    if (themeName.empty()) return {};
    if (themeName.find(L'\\') != std::wstring::npos || themeName.find(L'/') != std::wstring::npos) {
        return themeName;
    }
    return GetThemesDir() + L"\\" + themeName;
}

inline std::wstring ResolveRelativePath(const std::wstring& baseFile, const std::wstring& relOrAbs) {
    if (relOrAbs.empty()) return {};
    if (relOrAbs.size() >= 2 && relOrAbs[1] == L':') return relOrAbs;
    if (relOrAbs.rfind(L"\\\\", 0) == 0) return relOrAbs;
    std::wstring clean = relOrAbs;
    for (auto& ch : clean) {
        if (ch == L'/') ch = L'\\';
    }
    if (clean.rfind(L".\\", 0) == 0) clean = clean.substr(2);
    size_t slash = baseFile.find_last_of(L"\\/");
    std::wstring dir = (slash != std::wstring::npos) ? baseFile.substr(0, slash) : GetThemesDir();
    return dir + L"\\" + clean;
}

inline void EnsureDirRecursive(const std::wstring& dir) {
    if (dir.empty()) return;
    size_t pos = 0;
    while ((pos = dir.find_first_of(L"\\/", pos + 1)) != std::wstring::npos) {
        std::wstring sub = dir.substr(0, pos);
        if (sub.size() >= 2 && sub.back() == L':') continue;
        CreateDirectoryW(sub.c_str(), nullptr);
    }
    CreateDirectoryW(dir.c_str(), nullptr);
}

inline std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8] = {};
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

inline bool ReadTextFile(const std::wstring& path, std::string& out) {
    if (path.empty()) return false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER fsize{};
    if (!GetFileSizeEx(h, &fsize) || fsize.QuadPart <= 0 || fsize.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    DWORD size = static_cast<DWORD>(fsize.QuadPart);
    out.assign(size, '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(h, out.data(), size, &read, nullptr);
    CloseHandle(h);
    if (!ok || read == 0) return false;
    out.resize(read);
    return true;
}

// Atomic write via same-directory temp file + MoveFileExW(MOVEFILE_REPLACE_EXISTING)
// so concurrent readers never observe 0-byte or half-written JSON files.
inline bool WriteTextFile(const std::wstring& path, const std::string& data) {
    if (path.empty()) return false;
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        EnsureDirRecursive(path.substr(0, slash));
    }
    static volatile LONG s_tmpSeq = 0;
    LONG seq = InterlockedIncrement(&s_tmpSeq);
    std::wstring tmpPath = path + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
                           L"." + std::to_wstring(GetCurrentThreadId()) +
                           L"." + std::to_wstring(seq);
    HANDLE h = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = data.empty() ? TRUE : WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != data.size()) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // Fallback if MOVEFILE_WRITE_THROUGH is unsupported on some virtual drives
        if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmpPath.c_str());
            return false;
        }
    }
    return true;
}

inline ULONGLONG GetFileWriteTime(const std::wstring& path) {
    if (path.empty()) return 0;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    ULARGE_INTEGER ul{};
    ul.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    ul.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return ul.QuadPart;
}

namespace json_detail {

inline bool IsWs(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

inline bool ParseHex4(const std::string& s, size_t pos, uint32_t& out) {
    if (pos + 4 > s.size()) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i) {
        int d = HexDigit(s[pos + i]);
        if (d < 0) return false;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}

inline void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Reads a JSON string literal starting at `quotePos` (which must point to '"'),
// advancing `nextPos` past the closing '"'. Supports full \uXXXX + UTF-16 surrogate pairs.
inline bool ReadStringLiteral(const std::string& json, size_t quotePos, std::string& out, size_t& nextPos) {
    if (quotePos >= json.size() || json[quotePos] != '"') return false;
    size_t pos = quotePos + 1;
    std::string val;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char next = json[pos++];
            if (next == '"' || next == '\\' || next == '/') val.push_back(next);
            else if (next == 'b') val.push_back('\b');
            else if (next == 'f') val.push_back('\f');
            else if (next == 'n') val.push_back('\n');
            else if (next == 'r') val.push_back('\r');
            else if (next == 't') val.push_back('\t');
            else if (next == 'u') {
                uint32_t u1 = 0;
                if (!ParseHex4(json, pos, u1)) return false;
                pos += 4;
                if (u1 >= 0xD800 && u1 <= 0xDBFF && pos + 6 <= json.size() &&
                    json[pos] == '\\' && json[pos + 1] == 'u') {
                    uint32_t u2 = 0;
                    if (ParseHex4(json, pos + 2, u2) && u2 >= 0xDC00 && u2 <= 0xDFFF) {
                        pos += 6;
                        uint32_t cp = 0x10000 + (((u1 - 0xD800) << 10) | (u2 - 0xDC00));
                        AppendUtf8(val, cp);
                    } else {
                        AppendUtf8(val, u1);
                    }
                } else {
                    AppendUtf8(val, u1);
                }
            } else {
                val.push_back(next);
            }
        } else if (c == '"') {
            out = std::move(val);
            nextPos = pos;
            return true;
        } else {
            val.push_back(c);
        }
    }
    return false;
}

// Scans JSON tokens to find an actual object property key matching `key` followed by `:`.
// Never matches substrings inside string values or comments/notes.
inline bool FindKeyValuePos(const std::string& json, const char* key, size_t& outValPos) {
    if (!key) return false;
    size_t pos = 0;
    while (pos < json.size()) {
        if (json[pos] == '"') {
            std::string str;
            size_t afterStr = pos;
            if (!ReadStringLiteral(json, pos, str, afterStr)) return false;
            size_t p = afterStr;
            while (p < json.size() && IsWs(json[p])) ++p;
            if (p < json.size() && json[p] == ':') {
                if (str == key) {
                    ++p;
                    while (p < json.size() && IsWs(json[p])) ++p;
                    outValPos = p;
                    return true;
                }
            }
            pos = afterStr;
        } else {
            ++pos;
        }
    }
    return false;
}

} // namespace json_detail

inline bool ExtractJsonString(const std::string& json, const char* key, std::string& out) {
    size_t valPos = 0;
    if (!json_detail::FindKeyValuePos(json, key, valPos)) return false;
    size_t endPos = 0;
    return json_detail::ReadStringLiteral(json, valPos, out, endPos);
}

inline bool ExtractJsonBool(const std::string& json, const char* key, bool& out) {
    size_t valPos = 0;
    if (!json_detail::FindKeyValuePos(json, key, valPos)) return false;
    if (json.compare(valPos, 4, "true") == 0) { out = true; return true; }
    if (json.compare(valPos, 5, "false") == 0) { out = false; return true; }
    return false;
}

inline bool ExtractJsonFloat(const std::string& json, const char* key, float& out) {
    size_t valPos = 0;
    if (!json_detail::FindKeyValuePos(json, key, valPos)) return false;
    if (valPos < json.size() && json[valPos] == '"') ++valPos;
    char* end = nullptr;
    float v = strtof(json.c_str() + valPos, &end);
    if (end && end != json.c_str() + valPos && std::isfinite(v)) {
        out = v;
        return true;
    }
    return false;
}

inline bool ParseHexColorRef(const std::wstring& hex, COLORREF& outColor) {
    std::wstring s = hex;
    while (!s.empty() && (s.front() == L' ' || s.front() == L'#')) s.erase(s.begin());
    while (!s.empty() && s.back() == L' ') s.pop_back();
    if (s.size() != 6) return false;
    wchar_t* end = nullptr;
    unsigned long rgb = wcstoul(s.c_str(), &end, 16);
    if (!end || *end != L'\0') return false;
    outColor = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

inline std::wstring FormatColorRefHex(COLORREF c) {
    wchar_t buf[16] = {};
    swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

inline Overrides LoadOverrides(const std::wstring& path = GetOverridesPath()) {
    Overrides ov;
    std::string json;
    if (!ReadTextFile(path, json)) return ov;
    std::string tmp;
    if (ExtractJsonString(json, "cjk_font", tmp)) ov.cjk_font = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "latin_font", tmp)) ov.latin_font = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "mono_font", tmp)) ov.mono_font = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "custom_font_file", tmp)) ov.custom_font_file = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "border_color", tmp)) ov.border_color = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "caption_color", tmp)) ov.caption_color = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "wallpaper_file", tmp)) ov.wallpaper_file = Utf8ToWide(tmp);
    return ov;
}

inline bool SaveOverrides(const Overrides& ov, const std::wstring& path = GetOverridesPath()) {
    std::string json = "{\n";
    json += "  \"_note\": \"Non-empty values here override UI presets. Leave empty to use the UI or .theme.css setting.\",\n";
    json += "  \"cjk_font\": \"" + JsonEscape(WideToUtf8(ov.cjk_font)) + "\",\n";
    json += "  \"latin_font\": \"" + JsonEscape(WideToUtf8(ov.latin_font)) + "\",\n";
    json += "  \"mono_font\": \"" + JsonEscape(WideToUtf8(ov.mono_font)) + "\",\n";
    json += "  \"custom_font_file\": \"" + JsonEscape(WideToUtf8(ov.custom_font_file)) + "\",\n";
    json += "  \"border_color\": \"" + JsonEscape(WideToUtf8(ov.border_color)) + "\",\n";
    json += "  \"caption_color\": \"" + JsonEscape(WideToUtf8(ov.caption_color)) + "\",\n";
    json += "  \"wallpaper_file\": \"" + JsonEscape(WideToUtf8(ov.wallpaper_file)) + "\"\n";
    json += "}\n";
    return WriteTextFile(path, json);
}

inline void ParseConfigJsonInto(const std::string& json, Config& cfg) {
    std::string tmp;
    float fv = 0.0f;

    if (ExtractJsonString(json, "active_theme", tmp)) cfg.active_theme = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "wallpaper_file", tmp)) cfg.wallpaper_file = Utf8ToWide(tmp);
    if (ExtractJsonFloat(json, "wallpaper_opacity", fv) && fv >= 0.05f && fv <= 1.0f) cfg.wallpaper_opacity = fv;
    if (ExtractJsonFloat(json, "panel_opacity", fv) && fv >= 0.25f && fv <= 1.0f) cfg.panel_opacity = fv;
    ExtractJsonBool(json, "override_nitro", cfg.override_nitro);

    ExtractJsonBool(json, "font_enabled", cfg.font_enabled);
    if (ExtractJsonString(json, "font_mode", tmp)) {
        auto w = Utf8ToWide(tmp);
        if (w == L"cjk_fallback" || w == L"all") cfg.font_mode = w;
    }
    if (ExtractJsonString(json, "cjk_font", tmp)) cfg.cjk_font = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "latin_font", tmp)) cfg.latin_font = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "mono_font", tmp)) cfg.mono_font = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "custom_font_file", tmp)) cfg.custom_font_file = Utf8ToWide(tmp);
    if (ExtractJsonFloat(json, "font_scale", fv) && fv >= 0.5f && fv <= 2.0f) cfg.font_scale = fv;

    if (ExtractJsonFloat(json, "window_opacity", fv)) {
        int op = static_cast<int>(fv + 0.5f);
        if (op < 50) op = 50;
        if (op > 100) op = 100;
        cfg.window_opacity = op;
    }
    if (ExtractJsonString(json, "corner_style", tmp)) {
        auto w = Utf8ToWide(tmp);
        if (w == L"default" || w == L"round" || w == L"round_small" || w == L"square") cfg.corner_style = w;
    }
    if (ExtractJsonString(json, "border_color", tmp)) cfg.border_color = Utf8ToWide(tmp);
    if (ExtractJsonString(json, "caption_color", tmp)) cfg.caption_color = Utf8ToWide(tmp);
    ExtractJsonBool(json, "always_on_top", cfg.always_on_top);

    ExtractJsonBool(json, "keep_on_relaunch", cfg.keep_on_relaunch);
    ExtractJsonBool(json, "use_sunbird", cfg.use_sunbird);
    ExtractJsonBool(json, "auto_update_sunbird", cfg.auto_update_sunbird);
    if (ExtractJsonString(json, "action", tmp)) cfg.action = Utf8ToWide(tmp);
}

// Loads canonical %APPDATA%\phoenix-themes\config.json, merging the newer of the
// optional Sunbird adapter config if present, then layers overrides.json on top.
inline Config LoadConfig(const std::wstring& primaryPath = GetConfigPath(),
                         const std::wstring& adapterPath = GetSunbirdAdapterConfigPath(),
                         const std::wstring& overridesPath = GetOverridesPath()) {
    Config cfg;
    ULONGLONG tPrimary = GetFileWriteTime(primaryPath);
    ULONGLONG tAdapter = adapterPath.empty() ? 0 : GetFileWriteTime(adapterPath);

    std::string jsonPrimary, jsonAdapter;
    bool hasPrimary = (tPrimary > 0) && ReadTextFile(primaryPath, jsonPrimary);
    bool hasAdapter = (tAdapter > 0) && ReadTextFile(adapterPath, jsonAdapter);

    if (hasPrimary && hasAdapter) {
        if (tAdapter > tPrimary) {
            ParseConfigJsonInto(jsonPrimary, cfg);
            ParseConfigJsonInto(jsonAdapter, cfg);
        } else {
            ParseConfigJsonInto(jsonAdapter, cfg);
            ParseConfigJsonInto(jsonPrimary, cfg);
        }
    } else if (hasPrimary) {
        ParseConfigJsonInto(jsonPrimary, cfg);
    } else if (hasAdapter) {
        ParseConfigJsonInto(jsonAdapter, cfg);
    }

    if (!overridesPath.empty()) {
        Overrides ov = LoadOverrides(overridesPath);
        if (!ov.cjk_font.empty()) cfg.cjk_font = ov.cjk_font;
        if (!ov.latin_font.empty()) cfg.latin_font = ov.latin_font;
        if (!ov.mono_font.empty()) cfg.mono_font = ov.mono_font;
        if (!ov.custom_font_file.empty()) cfg.custom_font_file = ov.custom_font_file;
        if (!ov.border_color.empty()) cfg.border_color = ov.border_color;
        if (!ov.caption_color.empty()) cfg.caption_color = ov.caption_color;
        if (!ov.wallpaper_file.empty()) cfg.wallpaper_file = ov.wallpaper_file;
    }

    return cfg;
}

inline bool SaveConfig(const Config& cfg, const std::wstring& path = GetConfigPath()) {
    char scaleBuf[32] = {}, opBuf[32] = {}, wpOpBuf[32] = {}, panelOpBuf[32] = {};
    snprintf(scaleBuf, sizeof(scaleBuf), "%.2f", cfg.font_scale);
    snprintf(opBuf, sizeof(opBuf), "%d", cfg.window_opacity);
    snprintf(wpOpBuf, sizeof(wpOpBuf), "%.2f", cfg.wallpaper_opacity);
    snprintf(panelOpBuf, sizeof(panelOpBuf), "%.2f", cfg.panel_opacity);

    std::string json = "{\n";
    json += "  \"active_theme\": \"" + JsonEscape(WideToUtf8(cfg.active_theme)) + "\",\n";
    json += "  \"wallpaper_file\": \"" + JsonEscape(WideToUtf8(cfg.wallpaper_file)) + "\",\n";
    json += std::string("  \"wallpaper_opacity\": ") + wpOpBuf + ",\n";
    json += std::string("  \"panel_opacity\": ") + panelOpBuf + ",\n";
    json += std::string("  \"override_nitro\": ") + (cfg.override_nitro ? "true" : "false") + ",\n";
    json += std::string("  \"font_enabled\": ") + (cfg.font_enabled ? "true" : "false") + ",\n";
    json += "  \"font_mode\": \"" + JsonEscape(WideToUtf8(cfg.font_mode)) + "\",\n";
    json += "  \"cjk_font\": \"" + JsonEscape(WideToUtf8(cfg.cjk_font)) + "\",\n";
    json += "  \"latin_font\": \"" + JsonEscape(WideToUtf8(cfg.latin_font)) + "\",\n";
    json += "  \"mono_font\": \"" + JsonEscape(WideToUtf8(cfg.mono_font)) + "\",\n";
    json += "  \"custom_font_file\": \"" + JsonEscape(WideToUtf8(cfg.custom_font_file)) + "\",\n";
    json += std::string("  \"font_scale\": ") + scaleBuf + ",\n";
    json += std::string("  \"window_opacity\": ") + opBuf + ",\n";
    json += "  \"corner_style\": \"" + JsonEscape(WideToUtf8(cfg.corner_style)) + "\",\n";
    json += "  \"border_color\": \"" + JsonEscape(WideToUtf8(cfg.border_color)) + "\",\n";
    json += "  \"caption_color\": \"" + JsonEscape(WideToUtf8(cfg.caption_color)) + "\",\n";
    json += std::string("  \"always_on_top\": ") + (cfg.always_on_top ? "true" : "false") + ",\n";
    json += std::string("  \"keep_on_relaunch\": ") + (cfg.keep_on_relaunch ? "true" : "false") + ",\n";
    json += std::string("  \"use_sunbird\": ") + (cfg.use_sunbird ? "true" : "false") + ",\n";
    json += std::string("  \"auto_update_sunbird\": ") + (cfg.auto_update_sunbird ? "true" : "false") + "\n";
    json += "}\n";
    return WriteTextFile(path, json);
}

} // namespace phoenix_themes
