// @description Live .theme.css engine, custom wallpapers, custom fonts (.ttf/.otf), and window tweaks for Discord Phoenix (thin Sunbird UI adapter for phoenix-themes).
// @version 0.1.0
// @author sh-ayu

const readOr = (key, fallback) => {
  const v = phoenix.storage.get(key);
  return v === undefined || v === null ? fallback : v;
};

const clampNumber = (v, min, max, fallback) => {
  const n = Number(v);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(max, Math.max(min, n));
};

const toPreset = (raw, presets, fallback, digits = 0) => {
  const n = Number(raw);
  if (!Number.isFinite(n)) return fallback;
  const s = digits > 0 ? n.toFixed(digits) : String(Math.round(n));
  return presets.includes(s) ? s : fallback;
};

// Keep a local mirror of UI-managed keys so resetWindow never resurrects stale settings.* values
const localState = {};

const writeManagedKey = (key, value) => {
  localState[key] = value;
  phoenix.storage.set(key, value);
};

const triggerAction = (name) => {
  phoenix.storage.set("action", `${name}:${Date.now()}`);
};

const settings = phoenix.settings.define({
  // --- Live .theme.css Theme Engine ---
  activeTheme: {
    type: "select",
    title: "Theme (.theme.css)",
    description: "Live GPUI D3D11 CSS theme from %APPDATA%\\phoenix-themes\\themes (avatars & emojis untouched).",
    options: [
      { value: "", label: "Default (Discord Native)" },
      { value: "tokyo-night.theme.css", label: "Tokyo Night" },
      { value: "catppuccin-mocha.theme.css", label: "Catppuccin Mocha" },
      { value: "dracula.theme.css", label: "Dracula" },
      { value: "nord.theme.css", label: "Nord" },
      { value: "rose-pine.theme.css", label: "Rose Pine" },
      { value: "gruvbox-dark.theme.css", label: "Gruvbox Dark" },
      { value: "sakura-dark.theme.css", label: "Sakura Dark" },
      { value: "emerald-oled.theme.css", label: "Emerald OLED" },
      { value: "custom.theme.css", label: "Custom (custom.theme.css)" },
    ],
    default: String(readOr("active_theme", "")),
    onChange: (v) => writeManagedKey("active_theme", String(v)),
  },
  overrideNitro: {
    type: "switch",
    title: "Override Nitro Gradient Background",
    description: "Seal off Discord's Nitro gradient layer when no wallpaper is active so theme colors stay pure.",
    default: Boolean(readOr("override_nitro", true)),
    onChange: (v) => writeManagedKey("override_nitro", Boolean(v)),
  },
  editActiveTheme: {
    type: "button",
    title: "Edit Active .theme.css",
    description: "Open the active .theme.css in Notepad. Save (Ctrl+S) to hot-reload live!",
    onPress: () => triggerAction("edit_active_theme"),
  },
  openThemesDir: {
    type: "button",
    title: "Open Themes Folder",
    description: "Open %APPDATA%\\phoenix-themes\\themes to add, edit, or share .theme.css files.",
    onPress: () => triggerAction("open_themes_dir"),
  },

  // --- Custom Background Wallpaper (D3D11 Cover-Fit) ---
  pickWallpaper: {
    type: "button",
    title: "Pick Background Wallpaper Image...",
    description: "Choose any PNG / JPG / WebP image to display behind translucent Discord panels (applied live).",
    onPress: () => triggerAction("pick_wallpaper"),
  },
  clearWallpaper: {
    type: "button",
    title: "Clear Background Wallpaper",
    description: "Remove the custom background wallpaper image (applied live).",
    onPress: () => triggerAction("clear_wallpaper"),
  },
  wallpaperOpacity: {
    type: "select",
    title: "Wallpaper Brightness / Opacity",
    description: "Blend intensity of the background wallpaper (used unless overridden by CSS --background-opacity).",
    options: [
      { value: "0.15", label: "15% (Subtle)" },
      { value: "0.25", label: "25%" },
      { value: "0.35", label: "35% (Default)" },
      { value: "0.50", label: "50%" },
      { value: "0.65", label: "65%" },
      { value: "0.80", label: "80%" },
      { value: "1.00", label: "100% (Full)" },
    ],
    default: toPreset(readOr("wallpaper_opacity", 0.35), ["0.15", "0.25", "0.35", "0.50", "0.65", "0.80", "1.00"], "0.35", 2),
    onChange: (v) => writeManagedKey("wallpaper_opacity", clampNumber(v, 0.05, 1.0, 0.35)),
  },
  panelOpacity: {
    type: "select",
    title: "Panel Glass Opacity (with Wallpaper)",
    description: "Opacity of chat & sidebar panels when a wallpaper is active (used unless CSS sets --panel-opacity).",
    options: [
      { value: "0.45", label: "45% (Very Transparent)" },
      { value: "0.60", label: "60%" },
      { value: "0.70", label: "70%" },
      { value: "0.78", label: "78% (Default)" },
      { value: "0.85", label: "85%" },
      { value: "0.92", label: "92% (Subtle Glass)" },
    ],
    default: toPreset(readOr("panel_opacity", 0.78), ["0.45", "0.60", "0.70", "0.78", "0.85", "0.92"], "0.78", 2),
    onChange: (v) => writeManagedKey("panel_opacity", clampNumber(v, 0.25, 1.0, 0.78)),
  },

  // --- Live Window Appearance ---
  windowOpacity: {
    type: "select",
    title: "Window Opacity",
    description: "Whole-window transparency (applied live).",
    options: [
      { value: "100", label: "100% (Default)" },
      { value: "95", label: "95%" },
      { value: "90", label: "90%" },
      { value: "85", label: "85%" },
      { value: "80", label: "80%" },
      { value: "75", label: "75%" },
      { value: "65", label: "65%" },
    ],
    default: toPreset(readOr("window_opacity", 100), ["100", "95", "90", "85", "80", "75", "65"], "100", 0),
    onChange: (v) => writeManagedKey("window_opacity", Math.round(clampNumber(v, 50, 100, 100))),
  },
  cornerStyle: {
    type: "select",
    title: "Window Corners",
    description: "Windows 11 window corner style (applied live).",
    options: [
      { value: "default", label: "System Default" },
      { value: "round", label: "Round" },
      { value: "round_small", label: "Small Round" },
      { value: "square", label: "Square (Sharp)" },
    ],
    default: String(readOr("corner_style", "default")),
    onChange: (v) => writeManagedKey("corner_style", String(v)),
  },
  borderColor: {
    type: "select",
    title: "Window Border Color",
    description: "Windows 11 outer border color (applied live).",
    options: [
      { value: "", label: "System Default" },
      { value: "none", label: "Hidden (Borderless)" },
      { value: "#7AA2F7", label: "Tokyo Blue (#7AA2F7)" },
      { value: "#CBA6F7", label: "Catppuccin Mauve (#CBA6F7)" },
      { value: "#34D399", label: "Emerald (#34D399)" },
      { value: "#FF79C6", label: "Sakura Pink (#FF79C6)" },
      { value: "#5865F2", label: "Discord Blurple (#5865F2)" },
      { value: "#FFFFFF", label: "White (#FFFFFF)" },
    ],
    default: String(readOr("border_color", "")),
    onChange: (v) => {
      writeManagedKey("border_color", String(v));
      triggerAction("clear_border_override");
    },
  },
  pickBorderColor: {
    type: "button",
    title: "Pick Custom Border Color...",
    description: "Open Windows color picker to choose any custom border color live.",
    onPress: () => triggerAction("pick_border_color"),
  },
  alwaysOnTop: {
    type: "switch",
    title: "Always on Top",
    description: "Keep Discord Phoenix above other windows (applied live).",
    default: Boolean(readOr("always_on_top", false)),
    onChange: (v) => writeManagedKey("always_on_top", Boolean(v)),
  },

  // --- Custom Fonts (press Ctrl+R after changes) ---
  fontEnabled: {
    type: "switch",
    title: "Enable Custom Fonts",
    description: "Override DirectWrite fonts inside Discord Phoenix (press Ctrl+R to apply).",
    default: Boolean(readOr("font_enabled", true)),
    onChange: (v) => writeManagedKey("font_enabled", Boolean(v)),
  },
  fontMode: {
    type: "select",
    title: "Font Mode",
    description: "Keep gg sans for Latin/ASCII, or replace all UI fonts globally (Ctrl+R to apply).",
    options: [
      { value: "cjk_fallback", label: "CJK / Japanese Fallback Only (keep gg sans)" },
      { value: "all", label: "Replace All UI Fonts (Latin + CJK)" },
    ],
    default: String(readOr("font_mode", "cjk_fallback")),
    onChange: (v) => writeManagedKey("font_mode", String(v)),
  },
  cjkFont: {
    type: "select",
    title: "CJK / Japanese Font",
    description: "System CJK font preset (Ctrl+R to apply).",
    options: [
      { value: "Meiryo", label: "Meiryo" },
      { value: "Meiryo UI", label: "Meiryo UI" },
      { value: "BIZ UDPGothic", label: "BIZ UDPGothic" },
      { value: "BIZ UDGothic", label: "BIZ UDGothic" },
      { value: "Yu Gothic UI", label: "Yu Gothic UI" },
      { value: "Yu Gothic", label: "Yu Gothic" },
      { value: "Noto Sans JP", label: "Noto Sans JP" },
      { value: "MS PGothic", label: "MS PGothic" },
      { value: "Microsoft YaHei UI", label: "Microsoft YaHei UI (SC)" },
      { value: "Microsoft JhengHei UI", label: "Microsoft JhengHei UI (TC)" },
      { value: "Malgun Gothic", label: "Malgun Gothic (KR)" },
    ],
    default: String(readOr("cjk_font", "Meiryo")),
    onChange: (v) => writeManagedKey("cjk_font", String(v)),
  },
  monoFont: {
    type: "select",
    title: "Monospace / Code Font (Replace All mode)",
    description: "Optional override for gg sans mono (Ctrl+R to apply).",
    options: [
      { value: "", label: "Default (gg sans mono)" },
      { value: "Consolas", label: "Consolas" },
      { value: "Cascadia Mono", label: "Cascadia Mono" },
      { value: "Cascadia Code", label: "Cascadia Code" },
      { value: "JetBrains Mono", label: "JetBrains Mono" },
      { value: "BIZ UDGothic", label: "BIZ UDGothic" },
    ],
    default: String(readOr("mono_font", "")),
    onChange: (v) => writeManagedKey("mono_font", String(v)),
  },
  fontScale: {
    type: "select",
    title: "Font Scale",
    description: "Scale factor for overridden fonts (Ctrl+R to apply).",
    options: [
      { value: "0.85", label: "0.85x" },
      { value: "0.90", label: "0.90x" },
      { value: "0.95", label: "0.95x" },
      { value: "1.00", label: "1.00x (Default)" },
      { value: "1.05", label: "1.05x" },
      { value: "1.10", label: "1.10x" },
      { value: "1.15", label: "1.15x" },
      { value: "1.20", label: "1.20x" },
    ],
    default: toPreset(readOr("font_scale", 1.0), ["0.85", "0.90", "0.95", "1.00", "1.05", "1.10", "1.15", "1.20"], "1.00", 2),
    onChange: (v) => writeManagedKey("font_scale", clampNumber(v, 0.5, 2.0, 1.0)),
  },
  pickFontFile: {
    type: "button",
    title: "Pick Local .ttf / .otf Font File...",
    description: "Load any .ttf or .otf file in-memory without OS install (Ctrl+R to apply).",
    onPress: () => triggerAction("pick_font"),
  },
  clearFontFile: {
    type: "button",
    title: "Clear Local .ttf / .otf Font File",
    description: "Fall back to the preset font above (Ctrl+R to apply).",
    onPress: () => triggerAction("clear_font"),
  },
  openConfig: {
    type: "button",
    title: "Open Custom Overrides (JSON)",
    description: "Open %APPDATA%\\phoenix-themes\\overrides.json in Notepad.",
    onPress: () => triggerAction("open_config"),
  },

  // --- Relaunch & Loader Interop ---
  keepOnRelaunch: {
    type: "switch",
    title: "Keep Loaded on Ctrl+R Relaunch",
    description: "Automatically re-inject Phoenix Themes & Sunbird when Discord restarts itself.",
    default: Boolean(readOr("keep_on_relaunch", true)),
    onChange: (v) => writeManagedKey("keep_on_relaunch", Boolean(v)),
  },
  autoUpdateSunbird: {
    type: "switch",
    title: "Auto-Update Sunbird (phoenix.dll)",
    description: "Keep optional Sunbird runtime up to date on launcher startup.",
    default: Boolean(readOr("auto_update_sunbird", true)),
    onChange: (v) => writeManagedKey("auto_update_sunbird", Boolean(v)),
  },
  resetWindow: {
    type: "button",
    title: "Reset Appearance & Wallpaper",
    description: "Clear wallpaper, restore opacity/corners/border, and disable Always on Top.",
    onPress: () => {
      writeManagedKey("window_opacity", 100);
      writeManagedKey("corner_style", "default");
      writeManagedKey("border_color", "");
      writeManagedKey("always_on_top", false);
      try {
        settings.windowOpacity = "100";
        settings.cornerStyle = "default";
        settings.borderColor = "";
        settings.alwaysOnTop = false;
      } catch (_) {}
      triggerAction("reset_window");
    },
  },
});

phoenix.log.info("Phoenix Themes Sunbird adapter loaded");
