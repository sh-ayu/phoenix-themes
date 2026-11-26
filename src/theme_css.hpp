#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace phoenix_theme {

struct Hsla {
    float h = 0.0f; // 0..1
    float s = 0.0f; // 0..1
    float l = 0.0f; // 0..1
    float a = 1.0f; // 0..1
};

struct ColorStop {
    bool set = false;
    Hsla color{};
};

struct Theme {
    bool active = false;
    std::string name;

    // Neutral dark surface ramp (deepest -> brightest)
    ColorStop bg_tertiary;  // Server bar / deepest chrome
    ColorStop bg_secondary; // Sidebar / secondary panels
    ColorStop bg_primary;   // Main chat / primary cards
    ColorStop bg_elevated;  // Hover / inputs / floating

    // Accent (replaces Discord Blurple)
    ColorStop accent;

    // UI borders
    ColorStop ui_border;

    // Text & monochrome icons
    ColorStop text_normal;
    ColorStop text_muted;

    // Geometry tweaks
    bool has_radius_scale = false;
    float radius_scale = 1.0f; // 0.0 .. 2.5

    // Background image & translucency (CSS --background-image / --background-opacity / --panel-opacity)
    std::string background_image; // raw path from url(...)
    bool has_background_opacity = false;
    float background_opacity = 0.35f; // 0.05..1.0
    bool has_panel_opacity = false;
    float panel_opacity = 0.78f; // 0.25..1.0

    // Eliminate Nitro gradient bleed-through when no wallpaper is active
    bool override_nitro = true;

    // Optional theme-embedded window/font hints
    std::string window_border_color;
    std::string window_corners;
    std::string font_cjk;
    std::string font_code;
};

inline float Clamp01(float v) {
    if (!(v == v)) return 0.0f; // NaN guard
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline Hsla RgbToHsla(float r, float g, float b, float a = 1.0f) {
    r = Clamp01(r);
    g = Clamp01(g);
    b = Clamp01(b);
    float mx = (std::max)({r, g, b});
    float mn = (std::min)({r, g, b});
    float d = mx - mn;
    Hsla out{};
    out.l = 0.5f * (mx + mn);
    out.a = Clamp01(a);
    if (d <= 1e-6f) {
        out.h = 0.0f;
        out.s = 0.0f;
        return out;
    }
    out.s = (out.l > 0.5f) ? (d / (2.0f - mx - mn)) : (d / (mx + mn));
    float h = 0.0f;
    if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    out.h = h / 6.0f;
    if (out.h < 0.0f) out.h += 1.0f;
    if (out.h >= 1.0f) out.h -= 1.0f;
    return out;
}

inline void HslaToRgb(const Hsla& c, float& r, float& g, float& b) {
    auto hue2rgb = [](float p, float q, float t) -> float {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 0.5f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };
    if (c.s <= 1e-6f) {
        r = g = b = Clamp01(c.l);
        return;
    }
    float q = (c.l < 0.5f) ? (c.l * (1.0f + c.s)) : (c.l + c.s - c.l * c.s);
    float p = 2.0f * c.l - q;
    r = Clamp01(hue2rgb(p, q, c.h + 1.0f / 3.0f));
    g = Clamp01(hue2rgb(p, q, c.h));
    b = Clamp01(hue2rgb(p, q, c.h - 1.0f / 3.0f));
}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float LerpHue(float h1, float h2, float t) {
    float diff = h2 - h1;
    if (diff > 0.5f) diff -= 1.0f;
    if (diff < -0.5f) diff += 1.0f;
    float h = h1 + diff * t;
    if (h < 0.0f) h += 1.0f;
    if (h >= 1.0f) h -= 1.0f;
    return h;
}

inline Hsla LerpHsla(const Hsla& a, const Hsla& b, float t) {
    t = Clamp01(t);
    Hsla out{};
    if (a.s < 0.02f) out.h = b.h;
    else if (b.s < 0.02f) out.h = a.h;
    else out.h = LerpHue(a.h, b.h, t);
    out.s = Lerp(a.s, b.s, t);
    out.l = Lerp(a.l, b.l, t);
    out.a = Lerp(a.a, b.a, t);
    return out;
}

inline std::string TrimAscii(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

inline std::string UnquoteAscii(std::string s) {
    s = TrimAscii(s);
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

inline std::string LowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

inline std::string ParseCssUrl(const std::string& raw) {
    std::string s = TrimAscii(raw);
    std::string low = LowerAscii(s);
    if (low == "none" || low.empty()) return {};
    if (low.rfind("url(", 0) == 0) {
        size_t lp = s.find('(');
        size_t rp = s.rfind(')');
        if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
            return UnquoteAscii(s.substr(lp + 1, rp - lp - 1));
        }
    }
    return UnquoteAscii(s);
}

inline bool ParseCssColor(const std::string& raw, Hsla& out) {
    std::string s = LowerAscii(TrimAscii(raw));
    if (s.empty()) return false;

    if (s[0] == '#') {
        std::string hex = s.substr(1);
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return -1;
        };
        if (hex.size() == 3) {
            int r = hexVal(hex[0]), g = hexVal(hex[1]), b = hexVal(hex[2]);
            if (r < 0 || g < 0 || b < 0) return false;
            out = RgbToHsla((r * 17) / 255.0f, (g * 17) / 255.0f, (b * 17) / 255.0f, 1.0f);
            return true;
        }
        if (hex.size() == 6 || hex.size() == 8) {
            int v[8] = {};
            for (size_t i = 0; i < hex.size(); ++i) {
                v[i] = hexVal(hex[i]);
                if (v[i] < 0) return false;
            }
            float r = (v[0] * 16 + v[1]) / 255.0f;
            float g = (v[2] * 16 + v[3]) / 255.0f;
            float b = (v[4] * 16 + v[5]) / 255.0f;
            float a = (hex.size() == 8) ? ((v[6] * 16 + v[7]) / 255.0f) : 1.0f;
            out = RgbToHsla(r, g, b, a);
            return true;
        }
        return false;
    }

    if (s.rfind("rgb(", 0) == 0 || s.rfind("rgba(", 0) == 0) {
        size_t lp = s.find('(');
        size_t rp = s.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp) return false;
        std::string args = s.substr(lp + 1, rp - lp - 1);
        float vals[4] = {0, 0, 0, 1};
        bool isPct[4] = {false, false, false, false};
        int count = 0;
        const char* p = args.c_str();
        while (*p && count < 4) {
            while (*p == ' ' || *p == ',' || *p == '/' || *p == '\t') ++p;
            if (!*p) break;
            char* end = nullptr;
            float v = strtof(p, &end);
            if (end == p || !std::isfinite(v)) return false;
            p = end;
            if (*p == '%') { isPct[count] = true; ++p; }
            vals[count] = v;
            ++count;
        }
        if (count < 3) return false;
        float r = isPct[0] ? (vals[0] / 100.0f) : (vals[0] / 255.0f);
        float g = isPct[1] ? (vals[1] / 100.0f) : (vals[1] / 255.0f);
        float b = isPct[2] ? (vals[2] / 100.0f) : (vals[2] / 255.0f);
        float a = (count >= 4) ? (isPct[3] ? (vals[3] / 100.0f) : vals[3]) : 1.0f;
        out = RgbToHsla(r, g, b, a);
        return true;
    }

    if (s.rfind("hsl(", 0) == 0 || s.rfind("hsla(", 0) == 0) {
        size_t lp = s.find('(');
        size_t rp = s.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp) return false;
        std::string args = s.substr(lp + 1, rp - lp - 1);
        float vals[4] = {0, 0, 0, 1};
        bool isPct[4] = {false, false, false, false};
        float hueTurns = 0.0f;
        int count = 0;
        const char* p = args.c_str();
        while (*p && count < 4) {
            while (*p == ' ' || *p == ',' || *p == '/' || *p == '\t') ++p;
            if (!*p) break;
            char* end = nullptr;
            float v = strtof(p, &end);
            if (end == p || !std::isfinite(v)) return false;
            p = end;
            if (count == 0) {
                if (strncmp(p, "deg", 3) == 0) { hueTurns = v / 360.0f; p += 3; }
                else if (strncmp(p, "turn", 4) == 0) { hueTurns = v; p += 4; }
                else if (strncmp(p, "grad", 4) == 0) { hueTurns = v / 400.0f; p += 4; }
                else if (strncmp(p, "rad", 3) == 0) { hueTurns = v / 6.283185307f; p += 3; }
                else { hueTurns = v / 360.0f; }
            } else if (*p == '%') {
                isPct[count] = true;
                ++p;
            }
            vals[count] = v;
            ++count;
        }
        if (count < 3) return false;
        float h = fmodf(hueTurns, 1.0f);
        if (h < 0.0f) h += 1.0f;
        float a = (count >= 4) ? (isPct[3] ? (vals[3] / 100.0f) : vals[3]) : 1.0f;
        out = {h, Clamp01(vals[1] / 100.0f), Clamp01(vals[2] / 100.0f), Clamp01(a)};
        return true;
    }

    return false;
}

inline Theme ParseThemeCss(const std::string& cssText) {
    Theme t;
    std::string clean;
    clean.reserve(cssText.size());
    for (size_t i = 0; i < cssText.size();) {
        if (i + 1 < cssText.size() && cssText[i] == '/' && cssText[i + 1] == '*') {
            size_t end = cssText.find("*/", i + 2);
            if (end == std::string::npos) break;
            std::string comment = cssText.substr(i + 2, end - (i + 2));
            size_t np = comment.find("@name");
            if (np != std::string::npos && t.name.empty()) {
                size_t nl = comment.find_first_of("\r\n", np);
                t.name = TrimAscii(comment.substr(np + 5, nl == std::string::npos ? std::string::npos : nl - (np + 5)));
            }
            i = end + 2;
        } else {
            clean.push_back(cssText[i++]);
        }
    }

    std::unordered_map<std::string, std::string> vars;
    size_t pos = 0;
    while (pos < clean.size()) {
        size_t dash = clean.find("--", pos);
        if (dash == std::string::npos) break;

        // Require `--` to begin a CSS property declaration (avoid matching `var(--foo)` or `url("a--b.png")`)
        bool validDeclStart = (dash == 0);
        if (!validDeclStart) {
            char prev = clean[dash - 1];
            validDeclStart = (prev == ' ' || prev == '\t' || prev == '\r' || prev == '\n' ||
                              prev == ';' || prev == '{');
        }
        if (!validDeclStart) {
            pos = dash + 2;
            continue;
        }

        size_t colon = clean.find(':', dash + 2);
        if (colon == std::string::npos) break;

        // Reject if any delimiter appears between `--name` and `:` (not a declaration key)
        size_t badDelim = clean.find_first_of(";{}()\"'", dash + 2);
        if (badDelim != std::string::npos && badDelim < colon) {
            pos = dash + 2;
            continue;
        }

        size_t semi = clean.find_first_of(";}", colon + 1);
        if (semi == std::string::npos) semi = clean.size();

        std::string key = LowerAscii(TrimAscii(clean.substr(dash, colon - dash)));
        std::string val = TrimAscii(clean.substr(colon + 1, semi - (colon + 1)));
        size_t imp = LowerAscii(val).find("!important");
        if (imp != std::string::npos) val = TrimAscii(val.substr(0, imp));
        if (!key.empty() && !val.empty()) {
            vars[key] = val;
        }
        pos = semi + 1;
    }

    auto readColor = [&](const char* primary, const char* alias1, const char* alias2, ColorStop& dst) {
        const char* keys[3] = {primary, alias1, alias2};
        for (const char* k : keys) {
            if (!k) continue;
            auto it = vars.find(k);
            if (it != vars.end()) {
                Hsla c{};
                if (ParseCssColor(it->second, c)) {
                    dst.set = true;
                    dst.color = c;
                    return;
                }
            }
        }
    };

    readColor("--bg-tertiary", "--background-tertiary", "--bg-base-tertiary", t.bg_tertiary);
    readColor("--bg-secondary", "--background-secondary", "--bg-base-secondary", t.bg_secondary);
    readColor("--bg-primary", "--background-primary", "--bg-base-primary", t.bg_primary);
    readColor("--bg-elevated", "--background-floating", "--background-modifier-hover", t.bg_elevated);
    readColor("--accent-color", "--brand-experiment", "--brand-500", t.accent);
    readColor("--ui-border-color", "--background-modifier-accent", "--border-subtle", t.ui_border);
    readColor("--text-normal", "--text-primary", nullptr, t.text_normal);
    readColor("--text-muted", "--text-secondary", nullptr, t.text_muted);

    if (t.bg_primary.set || t.bg_secondary.set || t.bg_tertiary.set || t.bg_elevated.set) {
        Hsla base = t.bg_primary.set ? t.bg_primary.color
                   : (t.bg_secondary.set ? t.bg_secondary.color
                   : (t.bg_tertiary.set ? t.bg_tertiary.color : t.bg_elevated.color));
        if (!t.bg_primary.set) t.bg_primary = {true, base};
        if (!t.bg_secondary.set) {
            Hsla c = t.bg_primary.color;
            c.l = Clamp01(c.l * 0.80f);
            t.bg_secondary = {true, c};
        }
        if (!t.bg_tertiary.set) {
            Hsla c = t.bg_secondary.color;
            c.l = Clamp01(c.l * 0.72f);
            t.bg_tertiary = {true, c};
        }
        if (!t.bg_elevated.set) {
            Hsla c = t.bg_primary.color;
            c.l = Clamp01(c.l + 0.08f);
            t.bg_elevated = {true, c};
        }
    }

    auto readFloatVar = [&](const char* k1, const char* k2, float minV, float maxV, float& dst, bool& flag) {
        const char* keys[2] = {k1, k2};
        for (const char* k : keys) {
            if (!k) continue;
            auto it = vars.find(k);
            if (it != vars.end()) {
                char* end = nullptr;
                float v = strtof(it->second.c_str(), &end);
                if (end && end != it->second.c_str() && std::isfinite(v)) {
                    if (*end == '%') v /= 100.0f;
                    if (v >= minV && v <= maxV) {
                        dst = v;
                        flag = true;
                        return;
                    }
                }
            }
        }
    };

    readFloatVar("--radius-scale", nullptr, 0.0f, 2.5f, t.radius_scale, t.has_radius_scale);
    readFloatVar("--background-opacity", "--background-image-opacity", 0.05f, 1.0f, t.background_opacity, t.has_background_opacity);
    readFloatVar("--panel-opacity", nullptr, 0.25f, 1.0f, t.panel_opacity, t.has_panel_opacity);

    if (auto it = vars.find("--background-image"); it != vars.end()) {
        t.background_image = ParseCssUrl(it->second);
    }
    if (auto it = vars.find("--override-nitro"); it != vars.end()) {
        std::string v = LowerAscii(it->second);
        t.override_nitro = (v != "false" && v != "0" && v != "off");
    }
    if (auto it = vars.find("--window-border-color"); it != vars.end()) {
        t.window_border_color = UnquoteAscii(it->second);
    }
    if (auto it = vars.find("--window-corners"); it != vars.end()) {
        t.window_corners = LowerAscii(UnquoteAscii(it->second));
    }
    if (auto it = vars.find("--font-cjk"); it != vars.end()) {
        t.font_cjk = UnquoteAscii(it->second);
    }
    if (auto it = vars.find("--font-code"); it != vars.end()) {
        t.font_code = UnquoteAscii(it->second);
    }

    t.active = t.bg_primary.set || t.accent.set || t.ui_border.set ||
               t.text_normal.set || t.text_muted.set || t.has_radius_scale ||
               !t.background_image.empty();
    return t;
}

inline bool IsDiscordBlurple(const Hsla& c) {
    return c.a > 0.01f && c.s >= 0.42f && c.h >= 0.59f && c.h <= 0.71f && c.l >= 0.30f && c.l <= 0.82f;
}

enum class QuadRole : int {
    Default = 0,
    FullWindowRoot = 1, // Covers the whole window (vpW x vpH)
    ServerRail = 2,     // Far-left vertical server list (~72px wide)
    Sidebar = 3,        // Left channel sidebar / right member list
    MainPane = 4,       // Large central chat / settings pane
    HeaderBar = 5,      // Top titlebar / channel header / bottom bar
};

// Smoothly remaps a GPUI Quad fill/gradient color in HSLA space.
// Uses QuadRole (from screen geometry vs swapchain viewport) so server rail, sidebars,
// and main chat ALWAYS form a cohesive palette — even over Discord Nitro themes!
inline void RemapQuadColor(const Theme& t, Hsla& c, bool isLargePanel = false,
                           bool forceBackground = false, bool hasWallpaper = false,
                           bool isFullWindowRoot = false, QuadRole role = QuadRole::Default) {
    if (!t.active || c.a <= 0.005f) return;

    if (isFullWindowRoot && role == QuadRole::Default) {
        role = QuadRole::FullWindowRoot;
    }

    const bool isStructuralRole = (role != QuadRole::Default);

    // 1. Brand / Blurple -> Accent Color (skip for structural backgrounds/rails so Nitro purple never turns neon blue!)
    if (!forceBackground && !isStructuralRole && t.accent.set && IsDiscordBlurple(c)) {
        float lDelta = c.l - 0.65f;
        c.h = t.accent.color.h;
        c.s = t.accent.color.s;
        c.l = Clamp01(t.accent.color.l + lDelta * 0.75f);
        return;
    }

    const bool isNeutralDark = (c.s <= 0.24f && c.l <= 0.42f);
    const bool isDeepThemeBg = (c.l <= 0.22f);
    const bool isLargeDarkPanel = ((isLargePanel || isStructuralRole) && c.l <= 0.50f);

    const bool isSurfaceCandidate = (forceBackground || isStructuralRole || isNeutralDark || isDeepThemeBg || isLargeDarkPanel);

    if (t.bg_primary.set && isSurfaceCandidate) {
        Hsla mapped{};
        if (role == QuadRole::ServerRail || role == QuadRole::HeaderBar) {
            mapped = t.bg_tertiary.color;
        } else if (role == QuadRole::Sidebar) {
            mapped = t.bg_secondary.color;
        } else if (role == QuadRole::FullWindowRoot || role == QuadRole::MainPane) {
            mapped = t.bg_primary.color;
        } else {
            constexpr float kL0 = 0.055f;
            constexpr float kL1 = 0.105f;
            constexpr float kL2 = 0.165f;
            constexpr float kL3 = 0.265f;

            const float clampedL = forceBackground ? (std::min)(c.l, kL2) : c.l;
            if (clampedL <= kL0) {
                mapped = t.bg_tertiary.color;
                float ratio = (kL0 > 0.0f) ? (clampedL / kL0) : 1.0f;
                mapped.l = Clamp01(mapped.l * (0.82f + 0.18f * ratio));
            } else if (clampedL <= kL1) {
                mapped = LerpHsla(t.bg_tertiary.color, t.bg_secondary.color, (clampedL - kL0) / (kL1 - kL0));
            } else if (clampedL <= kL2) {
                mapped = LerpHsla(t.bg_secondary.color, t.bg_primary.color, (clampedL - kL1) / (kL2 - kL1));
            } else if (clampedL <= kL3) {
                mapped = LerpHsla(t.bg_primary.color, t.bg_elevated.color, (clampedL - kL2) / (kL3 - kL2));
            } else {
                mapped = t.bg_elevated.color;
                mapped.l = Clamp01(mapped.l + (clampedL - kL3) * 0.45f);
            }
        }

        c.h = mapped.h;
        c.s = mapped.s;
        c.l = mapped.l;
    }

    // Apply wallpaper glass translucency even for wallpaper-only / accent-only CSS themes (where bg_primary is unset!)
    if (hasWallpaper && (isSurfaceCandidate || role == QuadRole::FullWindowRoot)) {
        if (role == QuadRole::FullWindowRoot) {
            c.a = 0.0f;
        } else if (role == QuadRole::MainPane) {
            c.a = (std::min)(c.a, Clamp01(t.panel_opacity * 0.25f));
        } else if (role == QuadRole::ServerRail || role == QuadRole::Sidebar || role == QuadRole::HeaderBar) {
            c.a = (std::min)(c.a, Clamp01(t.panel_opacity * 0.42f));
        } else if (isLargePanel && c.a >= 0.35f) {
            c.a = (std::min)(c.a, t.panel_opacity);
        }
    } else if (!hasWallpaper && t.bg_primary.set && isSurfaceCandidate &&
               t.override_nitro && (isLargePanel || isStructuralRole) && c.a >= 0.45f) {
        // Seal off any Nitro gradient underneath when no wallpaper is active
        c.a = 1.0f;
    }
}

inline void RemapBorderColor(const Theme& t, Hsla& c) {
    if (!t.active || c.a <= 0.005f) return;
    if (t.accent.set && IsDiscordBlurple(c)) {
        c.h = t.accent.color.h;
        c.s = t.accent.color.s;
        c.l = t.accent.color.l;
        return;
    }
    if (t.ui_border.set && c.s <= 0.24f) {
        c.h = t.ui_border.color.h;
        c.s = t.ui_border.color.s;
        c.l = t.ui_border.color.l;
    }
}

inline void RemapMonoColor(const Theme& t, Hsla& c) {
    if (!t.active || c.a <= 0.005f) return;

    if (t.accent.set && IsDiscordBlurple(c)) {
        float lDelta = c.l - 0.65f;
        c.h = t.accent.color.h;
        c.s = t.accent.color.s;
        c.l = Clamp01(t.accent.color.l + lDelta * 0.75f);
        return;
    }

    if (c.s <= 0.24f) {
        if (t.text_normal.set && c.l >= 0.72f) {
            float lDelta = c.l - 0.88f;
            c.h = t.text_normal.color.h;
            c.s = t.text_normal.color.s;
            c.l = Clamp01(t.text_normal.color.l + lDelta * 0.60f);
        } else if (t.text_muted.set && c.l >= 0.38f && c.l < 0.72f) {
            float lDelta = c.l - 0.56f;
            c.h = t.text_muted.color.h;
            c.s = t.text_muted.color.s;
            c.l = Clamp01(t.text_muted.color.l + lDelta * 0.65f);
        }
    }
}

} // namespace phoenix_theme
