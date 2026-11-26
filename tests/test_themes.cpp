#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwrite_3.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/config.hpp"
#include "../src/theme_css.hpp"

struct SimpleTextSource : public IDWriteTextAnalysisSource {
    std::wstring text;
    explicit SimpleTextSource(std::wstring t) : text(std::move(t)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 pos, WCHAR const** txt, UINT32* len) override {
        if (pos >= text.size()) { *txt = nullptr; *len = 0; return S_OK; }
        *txt = text.c_str() + pos;
        *len = static_cast<UINT32>(text.size() - pos);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32, WCHAR const** txt, UINT32* len) override {
        *txt = nullptr; *len = 0; return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32, UINT32* len, WCHAR const** loc) override {
        *loc = L"ja-JP";
        *len = static_cast<UINT32>(text.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32, UINT32* len, IDWriteNumberSubstitution** sub) override {
        *sub = nullptr;
        *len = static_cast<UINT32>(text.size());
        return S_OK;
    }
};

static std::wstring MapFirstFont(IDWriteFontFallback* fb, const wchar_t* sample, float* outScale = nullptr) {
    SimpleTextSource src(sample);
    UINT32 mappedLen = 0;
    IDWriteFont* font = nullptr;
    FLOAT scale = 1.0f;
    HRESULT hr = fb->MapCharacters(&src, 0, static_cast<UINT32>(wcslen(sample)), nullptr, L"gg sans",
                                   DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                   &mappedLen, &font, &scale);
    if (outScale) *outScale = scale;
    if (FAILED(hr) || !font) return L"<none>";
    IDWriteFontFamily* fam = nullptr;
    font->GetFontFamily(&fam);
    IDWriteLocalizedStrings* names = nullptr;
    fam->GetFamilyNames(&names);
    wchar_t buf[128] = {};
    UINT32 idx = 0;
    BOOL exists = FALSE;
    names->FindLocaleName(L"en-us", &idx, &exists);
    names->GetString(exists ? idx : 0, buf, 128);
    names->Release();
    fam->Release();
    font->Release();
    return buf;
}

static std::wstring GetFormatFamily(IDWriteTextFormat* tf) {
    if (!tf) return L"<null>";
    wchar_t buf[128] = {};
    tf->GetFontFamilyName(buf, 128);
    return buf;
}

static std::wstring FindInstalledCjkFont() {
    IDWriteFactory* iso = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&iso))) || !iso) {
        return L"Yu Gothic UI";
    }
    IDWriteFontCollection* sys = nullptr;
    std::wstring chosen = L"Yu Gothic UI";
    if (SUCCEEDED(iso->GetSystemFontCollection(&sys, FALSE)) && sys) {
        const wchar_t* candidates[] = {
            L"Meiryo", L"Yu Gothic", L"Yu Gothic UI", L"MS Gothic", L"Microsoft YaHei",
        };
        for (const auto* cand : candidates) {
            UINT32 idx = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(sys->FindFamilyName(cand, &idx, &exists)) && exists) {
                chosen = cand;
                break;
            }
        }
        sys->Release();
    }
    iso->Release();
    return chosen;
}

static std::wstring FindSampleTtf() {
    const wchar_t* candidates[] = {
        L"C:\\Windows\\Fonts\\consola.ttf",
        L"C:\\Windows\\Fonts\\arial.ttf",
        L"C:\\Windows\\Fonts\\segoeui.ttf",
    };
    for (const auto* c : candidates) {
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return {};
}

static bool TestBundledThemes(const std::wstring& dllPath) {
    // Derive dist\themes relative to dllPath (dist\phoenix-themes.dll)
    size_t slash = dllPath.find_last_of(L"\\/");
    std::wstring dir = (slash != std::wstring::npos) ? dllPath.substr(0, slash) : L".";
    std::wstring themesDir = dir + L"\\themes";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW((themesDir + L"\\*.theme.css").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("FAIL: No bundled .theme.css files found in %ls\n", themesDir.c_str());
        return false;
    }
    int count = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring full = themesDir + L"\\" + fd.cFileName;
            std::string css;
            if (!phoenix_themes::ReadTextFile(full, css)) {
                printf("FAIL: Could not read bundled theme %ls\n", full.c_str());
                FindClose(hFind);
                return false;
            }
            auto th = phoenix_theme::ParseThemeCss(css);
            if (!th.active || th.name.empty() || !th.bg_primary.set || !th.bg_secondary.set ||
                !th.bg_tertiary.set || !th.accent.set) {
                printf("FAIL: Invalid bundled theme %ls\n", full.c_str());
                FindClose(hFind);
                return false;
            }
            count++;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    if (count < 8) {
        printf("FAIL: Expected at least 8 bundled themes, found %d\n", count);
        return false;
    }
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        printf("Usage: test_themes.exe <dll_path> <cjk_fallback|all|custom_file>\n");
        return 2;
    }
    std::wstring dllPath = argv[1];
    std::wstring mode = argv[2];

    // 1. Hex color parsing, CSS parser edge cases, QuadRole / Border / Mono remapping, and bundled themes
    COLORREF col = 0;
    if (!phoenix_themes::ParseHexColorRef(L"#7AA2F7", col) || col != RGB(0x7A, 0xA2, 0xF7)) {
        printf("FAIL: ParseHexColorRef failed\n");
        return 1;
    }

    if (!TestBundledThemes(dllPath)) {
        return 1;
    }

    {
        // Test var(--...) safety, url("--"), deg/turn/% alpha, Vencord/BD aliases, and NaN resilience
        std::string sampleCss =
            "/* @name UnitTest Theme */\n"
            ":root {\n"
            "  --background-tertiary: #11111b;\n"
            "  --background-secondary: #181825;\n"
            "  --background-primary: rgb(30, 30, 46);\n"
            "  --bg-elevated: hsl(237deg, 16%, 23%);\n"
            "  background: var(--background-primary);\n"
            "  --brand-experiment: #ff79c6 !important;\n"
            "  --ui-border-color: rgba(122, 162, 247, 50%);\n"
            "  --text-normal: #fce8f3;\n"
            "  --text-muted: #9399b2;\n"
            "  --radius-scale: 1.25;\n"
            "  --background-image: url(\"./my--wall.png\");\n"
            "  --background-opacity: 0.40;\n"
            "  --panel-opacity: 0.72;\n"
            "  --override-nitro: true;\n"
            "}\n";
        auto th = phoenix_theme::ParseThemeCss(sampleCss);
        if (!th.active || th.name != "UnitTest Theme" || !th.bg_primary.set || !th.accent.set ||
            !th.ui_border.set || std::fabs(th.ui_border.color.a - 0.50f) > 0.02f ||
            !th.has_radius_scale || th.background_image != "./my--wall.png" ||
            std::fabs(th.background_opacity - 0.40f) > 0.01f || std::fabs(th.panel_opacity - 0.72f) > 0.01f) {
            printf("FAIL: ParseThemeCss failed\n");
            return 1;
        }
        // NaN guard in Clamp01
        if (phoenix_theme::Clamp01(std::nanf("")) != 0.0f) {
            printf("FAIL: Clamp01(NaN) did not return 0\n");
            return 1;
        }
        // Blurple -> Sakura Pink
        auto blurple = phoenix_theme::RgbToHsla(0x58 / 255.0f, 0x65 / 255.0f, 0xF2 / 255.0f, 1.0f);
        phoenix_theme::RemapQuadColor(th, blurple, false, false, false, false);
        if (std::fabs(blurple.h - th.accent.color.h) > 0.02f) {
            printf("FAIL: Blurple accent remap failed\n");
            return 1;
        }
        // Online green MUST stay green
        auto green = phoenix_theme::RgbToHsla(0x23 / 255.0f, 0xA5 / 255.0f, 0x5A / 255.0f, 1.0f);
        float origGreenH = green.h;
        phoenix_theme::RemapQuadColor(th, green, false, false, false, false);
        if (std::fabs(green.h - origGreenH) > 0.001f) {
            printf("FAIL: Semantic green was modified!\n");
            return 1;
        }
        // QuadRole mapping (ServerRail -> bg_tertiary, Sidebar -> bg_secondary, MainPane -> bg_primary)
        phoenix_theme::Hsla railCol{0.72f, 0.50f, 0.14f, 1.0f};
        phoenix_theme::RemapQuadColor(th, railCol, true, false, false, false, phoenix_theme::QuadRole::ServerRail);
        if (std::fabs(railCol.l - th.bg_tertiary.color.l) > 0.01f) {
            printf("FAIL: ServerRail role did not map to bg_tertiary\n");
            return 1;
        }
        phoenix_theme::Hsla sideCol{0.72f, 0.50f, 0.14f, 1.0f};
        phoenix_theme::RemapQuadColor(th, sideCol, true, false, false, false, phoenix_theme::QuadRole::Sidebar);
        if (std::fabs(sideCol.l - th.bg_secondary.color.l) > 0.01f) {
            printf("FAIL: Sidebar role did not map to bg_secondary\n");
            return 1;
        }
        // RemapBorderColor & RemapMonoColor
        phoenix_theme::Hsla bcol{0.0f, 0.05f, 0.25f, 1.0f};
        phoenix_theme::RemapBorderColor(th, bcol);
        if (std::fabs(bcol.h - th.ui_border.color.h) > 0.02f) {
            printf("FAIL: RemapBorderColor failed\n");
            return 1;
        }
        phoenix_theme::Hsla mcol{0.0f, 0.05f, 0.90f, 1.0f};
        phoenix_theme::RemapMonoColor(th, mcol);
        if (std::fabs(mcol.h - th.text_normal.color.h) > 0.02f) {
            printf("FAIL: RemapMonoColor failed\n");
            return 1;
        }
        // Nitro override seals semi-transparent large panel to a=1.0 when no wallpaper is active
        phoenix_theme::Hsla nitroSemi{0.61f, 0.53f, 0.08f, 0.75f};
        phoenix_theme::RemapQuadColor(th, nitroSemi, true, false, false, false);
        if (std::fabs(nitroSemi.a - 1.0f) > 0.001f) {
            printf("FAIL: override_nitro did not seal large panel alpha (got %.2f)\n", nitroSemi.a);
            return 1;
        }
        // With wallpaper active: full-window root becomes transparent (a=0), large panel becomes panel_opacity (0.72)
        phoenix_theme::Hsla rootQ{0.0f, 0.0f, 0.05f, 1.0f};
        phoenix_theme::RemapQuadColor(th, rootQ, true, false, true, true);
        if (rootQ.a > 0.001f) {
            printf("FAIL: Full-window root quad not transparent for wallpaper\n");
            return 1;
        }
        phoenix_theme::Hsla panelQ{0.0f, 0.0f, 0.14f, 1.0f};
        phoenix_theme::RemapQuadColor(th, panelQ, true, false, true, false);
        if (std::fabs(panelQ.a - 0.72f) > 0.01f) {
            printf("FAIL: Large panel did not adopt panel_opacity (got %.2f)\n", panelQ.a);
            return 1;
        }
        // Wallpaper-only CSS (no --bg-primary) must STILL make structural panels translucent!
        auto wpOnly = phoenix_theme::ParseThemeCss(":root { --background-image: url(\"./bg.png\"); --panel-opacity: 0.70; }");
        phoenix_theme::Hsla wpPane{0.0f, 0.0f, 0.12f, 1.0f};
        phoenix_theme::RemapQuadColor(wpOnly, wpPane, true, false, true, false, phoenix_theme::QuadRole::MainPane);
        if (wpPane.a >= 0.5f) {
            printf("FAIL: Wallpaper-only CSS did not make MainPane translucent (got %.2f)\n", wpPane.a);
            return 1;
        }
    }

    // 1b. JSON \uXXXX + key boundary safety verification
    {
        std::string trickyJson =
            "{\n"
            "  \"_note\": \"mentions \\\"active_theme\\\": \\\"fake.css\\\" inside a note\",\n"
            "  \"active_theme\": \"\\u65e5\\u672c\\u8a9e.theme.css\"\n"
            "}\n";
        std::string extracted;
        if (!phoenix_themes::ExtractJsonString(trickyJson, "active_theme", extracted) ||
            phoenix_themes::Utf8ToWide(extracted) != L"\u65E5\u672C\u8A9E.theme.css") {
            printf("FAIL: JSON Unicode/key-boundary parse failed\n");
            return 1;
        }
    }

    // 2. Loader-independent config & overrides layering verification
    std::wstring testCjkFont = FindInstalledCjkFont();

    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring fakeBase = std::wstring(tempDir) + L"phoenix-themes-test-" + mode;
    phoenix_themes::EnsureDirRecursive(fakeBase);
    SetEnvironmentVariableW(L"PHOENIX_THEMES_HOME", fakeBase.c_str());
    SetEnvironmentVariableW(L"PHOENIX_HOME", fakeBase.c_str());
    SetEnvironmentVariableW(L"PHOENIX_THEMES_NO_WATCHER", L"1");
    DeleteFileW(phoenix_themes::GetConfigPath().c_str());
    DeleteFileW(phoenix_themes::GetOverridesPath().c_str());
    DeleteFileW(phoenix_themes::GetSunbirdAdapterConfigPath().c_str());

    phoenix_themes::Config cfg;
    cfg.active_theme = L"tokyo-night.theme.css";
    cfg.font_enabled = true;
    cfg.cjk_font = testCjkFont;
    cfg.window_opacity = 90;
    cfg.corner_style = L"round";
    cfg.border_color = L"#7AA2F7";
    cfg.always_on_top = true;

    if (mode == L"cjk_fallback") {
        cfg.font_mode = L"cjk_fallback";
        cfg.font_scale = 0.95f;
    } else if (mode == L"all") {
        cfg.font_mode = L"all";
        cfg.latin_font = L"Segoe UI";
        cfg.mono_font = L"Consolas";
        cfg.font_scale = 1.05f;
    } else if (mode == L"custom_file") {
        cfg.font_mode = L"all";
        cfg.latin_font = L"";
        cfg.custom_font_file = FindSampleTtf();
        cfg.font_scale = 1.0f;
        if (cfg.custom_font_file.empty()) {
            printf("SKIP: no sample .ttf found\n");
            return 0;
        }
    }

    if (!phoenix_themes::SaveConfig(cfg)) {
        printf("FAIL: SaveConfig failed\n");
        return 1;
    }
    auto reloaded = phoenix_themes::LoadConfig();
    if (reloaded.active_theme != L"tokyo-night.theme.css" || reloaded.window_opacity != 90 ||
        reloaded.border_color != L"#7AA2F7") {
        printf("FAIL: Config roundtrip mismatch\n");
        return 1;
    }

    if (mode == L"custom_file") {
        phoenix_themes::Overrides ov;
        ov.custom_font_file = cfg.custom_font_file;
        ov.border_color = L"#34D399";
        phoenix_themes::SaveOverrides(ov);
        cfg.custom_font_file.clear();
        phoenix_themes::SaveConfig(cfg);
        auto layered = phoenix_themes::LoadConfig();
        if (layered.custom_font_file != ov.custom_font_file || layered.border_color != L"#34D399") {
            printf("FAIL: Overrides layering mismatch\n");
            return 1;
        }
    }

    // 3. DirectWrite IAT + COM vtable hook verification
    HMODULE h = LoadLibraryW(dllPath.c_str());
    if (!h) {
        printf("FAIL: LoadLibraryW failed (%lu)\n", GetLastError());
        return 1;
    }

    IDWriteFactory5* factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                     reinterpret_cast<IUnknown**>(&factory));
    if (FAILED(hr) || !factory) {
        printf("FAIL: DWriteCreateFactory failed\n");
        return 1;
    }

    IDWriteFontFallback* fb = nullptr;
    hr = factory->GetSystemFontFallback(&fb);
    if (FAILED(hr) || !fb) {
        printf("FAIL: GetSystemFontFallback failed\n");
        return 1;
    }

    float scaleOut = 0.0f;
    auto jpHira = MapFirstFont(fb, L"こんにちは", &scaleOut);
    auto emoji = MapFirstFont(fb, L"\U0001F600");

    IDWriteTextFormat* tfUi = nullptr;
    HRESULT hrUi = factory->CreateTextFormat(L"gg sans", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ja-JP", &tfUi);
    IDWriteTextFormat* tfMono = nullptr;
    HRESULT hrMono = factory->CreateTextFormat(L"gg sans mono", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ja-JP", &tfMono);
    IDWriteTextFormat* tfEmoji = nullptr;
    HRESULT hrEmoji = factory->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ja-JP", &tfEmoji);
    if (FAILED(hrUi) || !tfUi || FAILED(hrMono) || !tfMono || FAILED(hrEmoji) || !tfEmoji) {
        printf("FAIL: CreateTextFormat returned failure\n");
        return 1;
    }

    auto famUi = GetFormatFamily(tfUi);
    auto famMono = GetFormatFamily(tfMono);
    auto famEmoji = GetFormatFamily(tfEmoji);

    wprintf(L"[%ls] targetCjk=%ls Hiragana=%ls (scale=%.2f) Emoji=%ls | TextFormat ui=%ls mono=%ls emoji=%ls\n",
            mode.c_str(), testCjkFont.c_str(), jpHira.c_str(), scaleOut, emoji.c_str(),
            famUi.c_str(), famMono.c_str(), famEmoji.c_str());

    if (emoji != L"Segoe UI Emoji" || famEmoji != L"Segoe UI Emoji") {
        printf("FAIL: Emoji font broken\n");
        return 1;
    }

    if (mode == L"cjk_fallback") {
        if (jpHira != testCjkFont || famUi != L"gg sans" || std::fabs(scaleOut - 0.95f) > 0.01f) {
            printf("FAIL: cjk_fallback mismatch\n");
            return 1;
        }
    } else if (mode == L"all") {
        if (jpHira != testCjkFont || famUi != L"Segoe UI" || famMono != L"Consolas") {
            printf("FAIL: all mode mismatch\n");
            return 1;
        }
    } else if (mode == L"custom_file") {
        if (famUi == L"gg sans" || famUi.empty()) {
            printf("FAIL: custom_font_file did not override base family\n");
            return 1;
        }
    }

    tfUi->Release();
    tfMono->Release();
    tfEmoji->Release();
    fb->Release();
    factory->Release();

    DeleteFileW(phoenix_themes::GetConfigPath().c_str());
    DeleteFileW(phoenix_themes::GetOverridesPath().c_str());
    wprintf(L"PASS: %ls\n", mode.c_str());
    return 0;
}
