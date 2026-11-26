# Phoenix Themes

[![Build and Release](https://github.com/sh-ayu/phoenix-themes/actions/workflows/release.yml/badge.svg)](https://github.com/sh-ayu/phoenix-themes/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

[English README](README.md)

**Discord Phoenix**（Discord の Rust + GPUI 製ネイティブ Windows クライアント）向けの **Mod ローダー非依存 `.theme.css` テーマ基盤・背景壁紙エンジン・フォント＆ウィンドウカスタマイズツール** です。

- **`Discord.exe` のバイナリ改変なし**
- **固定オフセット 0 個**: Direct3D 11 / `D3DReflect` / DirectWrite の標準 API のみで動作するため、Discord Phoenix のアップデートに強い構造です。
- **Mod ローダー非依存**: テーマと設定は `%APPDATA%\phoenix-themes\` に独立保存されます。単体動作（`--no-sunbird`）にも対応しつつ、**[Sunbird](https://sunbird.tools/)** 連携時は Discord の設定画面から 1 クリック操作できます。

---

## 主な機能

1. **リアルタイム `.theme.css` テーマエンジン（GPUI Direct3D 11）**
   - `%APPDATA%\phoenix-themes\themes\` に `.theme.css` を置くだけで適用され、編集中に `Ctrl+S` で保存すると**再起動なしで即ホットリロード**されます。
   - UI の背景・サーバー一覧・サイドバー・アクセント・文字・枠線・角丸だけを綺麗に置換し、**アバター・添付画像・絵文字・ネームプレート・動画は一切崩しません**。
   - **Nitro グラデーション完全上書き**: Discord 側の Nitro テーマがオンのままでも下地が濁らないクリーンな発色を実現。
   - **8 種の同梱テーマ**: `Tokyo Night` / `Catppuccin Mocha` / `Dracula` / `Nord` / `Rose Pine` / `Gruvbox Dark` / `Sakura Dark` / `Emerald OLED`。
2. **カスタム背景壁紙（ネイティブ D3D11 Cover-Fit＋部分再描画対応）**
   - 好きな **PNG / JPG / WebP / BMP** 画像を `--background-image: url("./wallpaper.png");` または設定画面の **Pick Background Wallpaper Image...** から 1 クリック設定。
   - チャット欄やサイドバーが自動で半透明ガラス調（`--panel-opacity`）になり、文字・アバター・絵文字・ホバー・ツールチップも崩れず綺麗に背景画像が透けます。
3. **フォント差し替え（システムフォント＋ローカル `.ttf` / `.otf`）**
   - **日本語 / CJK のみ変更**: 英数字は `gg sans`、絵文字は `Segoe UI Emoji` のまま、日本語だけ `Meiryo` / `BIZ UDPGothic` / `Noto Sans JP` 等へ変更。
   - **全体差し替えモード（Replace All）**: UI 全体および等幅フォント（`gg sans mono`）を一括変更。
   - **ローカル `.ttf` / `.otf` 読み込み**: Windows にインストールせずオンメモリで読み込み可能。
4. **ライブウィンドウ外観＆`Ctrl+R` 自動再注入**
   - 透明度・Windows 11 角丸・枠線色（プリセット / 非表示 / カラーピッカー）・常に最前面をリアルタイム反映。
   - `Ctrl+R` 再起動時も Phoenix Themes（および併用中の Sunbird）を自動で維持します。

---

## 自作 `.theme.css` の書き方

`%APPDATA%\phoenix-themes\themes\my-theme.theme.css` を作成します：

```css
/* @name My Custom Theme
   @author your-name */

:root {
  --bg-tertiary: #16161e;
  --bg-secondary: #1a1b26;
  --bg-primary: #1f2335;
  --bg-elevated: #292e42;

  --accent-color: #7aa2f7;
  --ui-border-color: #3b4261;

  --text-normal: #c0caf5;
  --text-muted: #737aa2;

  --radius-scale: 1.0;

  /* 任意の背景画像（PNG / JPG / WebP） */
  /* --background-image: url("./wallpaper.png"); */
  /* --background-opacity: 0.35; */
  /* --panel-opacity: 0.78; */
}
```

BetterDiscord / Vencord 風の変数エイリアス（`--background-primary`、`--background-secondary`、`--brand-experiment` 等）もそのまま使えます。

---

## かんたん導入手順

1. Windows 版 **Discord Phoenix** をインストールしておきます。
2. [Releases](https://github.com/sh-ayu/phoenix-themes/releases) から `phoenix-themes-windows-x86_64.zip` をダウンロードして解凍します。
3. **`phoenix-themes.exe`** を 1 回実行します。
   - `%APPDATA%\phoenix-themes\` に本体と同梱テーマ 8 種を配置し、Sunbird 用 UI アダプタ（`%LOCALAPPDATA%\DiscordPhoenix\sunbird\plugins\phoenix-themes.js`）およびデスクトップ／スタートメニューの `Sunbird.lnk` ショートカットを更新します。
   - 既定設定（`use_sunbird: true`・`auto_update_sunbird: true`）では、Discord 内設定 UI を利用できるよう `https://assets.sunbird.tools` から Sunbird ランタイム（`phoenix.dll`）を自動取得・更新します。
4. Discord 内の **ユーザー設定 → Sunbird → Plugins → `phoenix-themes`** から自由にカスタマイズしてください。

### Sunbird なしの単体利用

Sunbird を取得・注入せずに単体で利用したい場合は `--no-sunbird` オプションで起動するか、`%APPDATA%\phoenix-themes\config.json` を以下のように作成してください：

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

### ランチャー CLI オプション

- `--no-sunbird`: Sunbird（`phoenix.dll`）を使わずに単体起動（`Ctrl+R` 再起動時も維持）。
- `--no-update`: 起動時の `assets.sunbird.tools` への更新チェックをスキップ。
- `--force-restart`: 起動中の Discord Phoenix があっても確認ダイアログなしで再起動。
- `--uninstall`: 導入した DLL・アダプタを削除し、ショートカットを復元または削除。

---

## ソースからビルドする（Windows x64）

必要な環境: **Visual Studio Build Tools (C++ x64)**、**Windows 10/11 SDK**、**Node.js**（`-Test` 時の JS 構文チェックに使用）。

```powershell
# /W4 /WX でビルド & 自動テスト実行
.\build.ps1 -Test

# ビルド・テスト・%APPDATA%\phoenix-themes へ配備・ショートカット更新
.\build.ps1 -Test -Install
```

## アンインストール

1. タスクトレイも含めて **Discord Phoenix を完全に終了**します。
2. 以下を実行します：

```cmd
%APPDATA%\phoenix-themes\phoenix-themes.exe --uninstall
```

`%APPDATA%\phoenix-themes\themes\` に保存した自作テーマは消さずに残ります。`Discord.exe` は改変されません。不要であれば実行後に `%APPDATA%\phoenix-themes\phoenix-themes.exe` 自体も削除できます。

## サードパーティライセンス・免責事項

- [MinHook](https://github.com/TsudaKageyu/minhook)（BSD-2-Clause、[third_party/minhook/LICENSE.txt](third_party/minhook/LICENSE.txt)、リリースアーカイブ内では `LICENSE-MinHook.txt`）を使用しています。
- Discord Inc. および Sunbird とは無関係の非公式ツールです。自己責任でご利用ください。

## ライセンス

[MIT](LICENSE)
