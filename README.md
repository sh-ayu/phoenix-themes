# Phoenix Themes

[![Build and Release](https://github.com/sh-ayu/phoenix-themes/actions/workflows/release.yml/badge.svg)](https://github.com/sh-ayu/phoenix-themes/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

[日本語版 README はこちら (README.ja.md)](README.ja.md)

A **mod-loader-independent `.theme.css` theme engine, wallpaper renderer, and font/window customization platform** for **Discord Phoenix** (Discord's native Rust + GPUI desktop client on Windows).

- **Zero binary patching.** `Discord.exe` is never modified on disk.
- **Zero hardcoded offsets.** Uses standard Windows COM/D3D11/DirectWrite APIs + runtime `D3DReflect` shader metadata so it survives Discord Phoenix updates.
- **Mod-loader independent.** Your themes and config live in `%APPDATA%\phoenix-themes\`. Works standalone (`--no-sunbird`) or alongside **[Sunbird](https://sunbird.tools/)** (with a bundled 1-click in-Discord settings UI adapter).

---

## Features

1. **Live `.theme.css` Engine (GPUI Direct3D 11)**
   - Drop any `.theme.css` file into `%APPDATA%\phoenix-themes\themes\` and edit it live — press `Ctrl+S` to hot-reload without restarting Discord.
   - Remaps UI surfaces, server rail, sidebars, borders, accents, text/icons, and corner radii cleanly while keeping **avatars, attached images, emojis, nameplates, and videos 100% untouched**.
   - **Nitro Gradient Override**: Automatically seals off Discord's Nitro gradient layer when no wallpaper is active so your theme colors stay pure.
   - **8 Bundled Starter Themes**: `Tokyo Night`, `Catppuccin Mocha`, `Dracula`, `Nord`, `Rose Pine`, `Gruvbox Dark`, `Sakura Dark`, `Emerald OLED`.
2. **Custom Background Wallpapers (Native D3D11 Cover-Fit + Dirty-Rect Support)**
   - Set any **PNG / JPG / WebP / BMP** image via CSS (`--background-image: url("./wallpaper.png");`) or the 1-click **Pick Background Wallpaper Image...** dialog.
   - Automatically turns chat & sidebar panels into translucent tinted glass (`--panel-opacity`) while keeping text, avatars, emojis, hover highlights, and tooltips artifact-free.
3. **Custom Fonts (System Fonts + In-Memory `.ttf` / `.otf`)**
   - **CJK / Japanese Fallback Mode**: Keep `gg sans` for Latin/ASCII and `Segoe UI Emoji` for emoji while switching CJK glyphs to `Meiryo`, `BIZ UDPGothic`, `Noto Sans JP`, etc.
   - **Replace All Mode**: Override global UI and monospace (`gg sans mono`) fonts.
   - **Local `.ttf` / `.otf` Loader**: Load any font file directly in-memory without installing it to Windows.
4. **Live Window Tweaks & `Ctrl+R` Relaunch Guard**
   - Window opacity, Windows 11 corners (`Round` / `Small Round` / `Square`), custom border colors (presets, borderless, or Windows Color Picker), and Always on Top — all applied live.
   - Hooks `CreateProcessW` so `Ctrl+R` reloads keep Phoenix Themes (and optional Sunbird) loaded automatically.

---

## Writing Your Own `.theme.css`

Create a file in `%APPDATA%\phoenix-themes\themes\my-theme.theme.css`:

```css
/* @name My Custom Theme
   @author your-name */

:root {
  /* Surface ramp (deepest -> elevated) */
  --bg-tertiary: #16161e;
  --bg-secondary: #1a1b26;
  --bg-primary: #1f2335;
  --bg-elevated: #292e42;

  /* Accent (replaces Discord Blurple) & borders */
  --accent-color: #7aa2f7;
  --ui-border-color: #3b4261;

  /* Text & monochrome icons */
  --text-normal: #c0caf5;
  --text-muted: #737aa2;

  /* UI corner radius multiplier (preserves circular avatars) */
  --radius-scale: 1.0;

  /* Optional wallpaper (relative to the .theme.css file, or absolute path) */
  /* --background-image: url("./wallpaper.png"); */
  /* --background-opacity: 0.35; */
  /* --panel-opacity: 0.78; */
}
```

Vencord/BetterDiscord-style variable aliases (`--background-primary`, `--background-secondary`, `--background-tertiary`, `--background-floating`, `--brand-experiment`) are also supported.

---

## Quick Start

1. Install **Discord Phoenix** (Windows x64).
2. Download `phoenix-themes-windows-x86_64.zip` from [Releases](https://github.com/sh-ayu/phoenix-themes/releases) and extract it.
3. Run **`phoenix-themes.exe`** once.
   - Installs core files & bundled `.theme.css` themes into `%APPDATA%\phoenix-themes\`.
   - Installs the Sunbird settings UI adapter (`%LOCALAPPDATA%\DiscordPhoenix\sunbird\plugins\phoenix-themes.js`) and updates the Desktop & Start Menu `Sunbird.lnk` shortcuts.
   - By default (`use_sunbird: true`, `auto_update_sunbird: true`), the launcher also fetches/updates the Sunbird runtime (`phoenix.dll`) from `https://assets.sunbird.tools` so you get an in-Discord Settings panel automatically.
4. Customize inside **Discord → User Settings → Sunbird → Plugins → `phoenix-themes`**.

### Standalone Mode (without Sunbird)

To run Phoenix Themes completely standalone without downloading or injecting Sunbird, pass `--no-sunbird` or set `"use_sunbird": false` in `%APPDATA%\phoenix-themes\config.json`:

```json
{
  "active_theme": "tokyo-night.theme.css",
  "wallpaper_file": "",
  "wallpaper_opacity": 0.35,
  "panel_opacity": 0.78,
  "use_sunbird": false,
  "auto_update_sunbird": false
}
```

### Launcher CLI Flags

- `--no-sunbird`: Launch without Sunbird (`phoenix.dll`), including across `Ctrl+R` reloads.
- `--no-update`: Skip checking `assets.sunbird.tools` for Sunbird updates on startup.
- `--force-restart`: Automatically restart an already running Discord Phoenix instance without prompting.
- `--uninstall`: Remove installed runtime files & adapter and restore/remove shortcuts.

---

## Build from Source (Windows x64)

Requires **Visual Studio Build Tools (C++ x64)**, **Windows 10/11 SDK**, and **Node.js** (for `-Test` JS syntax validation).

```powershell
# Build with /W4 /WX and run unit tests
.\build.ps1 -Test

# Build, test, install into %APPDATA%\phoenix-themes, and update shortcuts
.\build.ps1 -Test -Install
```

## Uninstall

1. Completely quit **Discord Phoenix** (including from the system tray).
2. Run:

```cmd
%APPDATA%\phoenix-themes\phoenix-themes.exe --uninstall
```

Your custom themes in `%APPDATA%\phoenix-themes\themes\` are preserved. `Discord.exe` is never modified. You may also delete `%APPDATA%\phoenix-themes\phoenix-themes.exe` afterwards if desired.

## Third-Party Licenses & Disclaimer

- Includes [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause, see [third_party/minhook/LICENSE.txt](third_party/minhook/LICENSE.txt), packaged as `LICENSE-MinHook.txt` in release archives).
- Not affiliated with Discord Inc. or Sunbird. Use at your own risk.

## License

[MIT](LICENSE)
