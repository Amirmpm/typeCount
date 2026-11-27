# TypeCount

**TypeCount** is a privacy-first, offline Windows desktop application for tracking keyboard and mouse activity, typing speed, ergonomic wellness, shortcuts, and per-application breakdowns — built with native Win32 Raw Input, Edge WebView2, and local SQLite storage.

## Download

Get the latest Windows x64 packages from the **[GitHub Releases](https://github.com/Amirmpm/typeCount/releases/latest)** page (or directly from [`releases/`](releases/)):

| Package | Best For | Details |
|---|---|---|
| **`TypeCount-Setup-x64.msi`** | Standard Windows Installer | MSI setup with in-place upgrade detection, Start Menu & Desktop shortcuts, and Apps & Features uninstaller (available on [GitHub Releases](https://github.com/Amirmpm/typeCount/releases/latest)) |
| **`TypeCount-Setup-x64.exe`** | Bilingual Setup Wizard (EN / فارسی) | Step-by-step setup wizard that detects existing installations, upgrades in-place without losing history, and registers Windows uninstall & startup options |
| **`TypeCount.exe`** | Portable Single Executable | Run directly anywhere on Windows 10/11 (x64) with zero installation required |

> **System Requirements:** Windows 10 or Windows 11 (64-bit) with the built-in Microsoft Edge WebView2 Runtime.

## What's New in v1.1.0

- **Installers & Clean Upgrades**: Native `TypeCount-Setup-x64.msi` and bilingual `TypeCount-Setup-x64.exe` setup packages that recognize existing versions, close running background instances gracefully, upgrade in-place, and register in Windows Installed Programs.
- **Built-In Typing Speed Test**: Dedicated Typing Test tab with English, Persian (`فارسی`), and Code passages, 15s / 30s / 60s presets, live WPM / CPM / accuracy / errors tracking, personal best records, and an option to exclude practice typing from main stats.
- **Ergonomic Insights & Strain Guard**: Real-time Focus Score (0–100), Active Time tracking, Peak Hour detection, 10-minute Typing Strain indicator, and configurable Break / 20-20-20 reminder intervals.
- **Smart Goal & Milestone Alerts + Sound Chimes**: Instant toast and optional gentle Windows chime notifications when hitting your daily goal, break interval, or strain guard threshold.
- **App Privacy Exclusions**: Exclude sensitive apps (such as password managers or private browsers) from attribution or pause counting altogether while they are focused.
- **Solar Hijri (Jalali / شمسی) Calendar Support**: Full Jalali date formatting across tooltips, heatmaps, charts, history, and report cards — toggleable independently or auto-enabled with Persian (`fa`).
- **True Background Idle Detection & In-App Updates**: Configurable idle timeout for accurate active-time stats, plus live GitHub Release update checking directly from Settings.
- **All New Features Toggleable**: Every capability has dedicated ON/OFF switches in Settings.

## Key Features

- **Native Windows Background Capture (`WM_INPUT`)**: Counts keystrokes, mouse clicks (Left / Right / Middle / Side Back / Side Forward), scroll wheel steps, cursor travel distance, and modifier shortcut chords with minimal CPU and memory overhead.
- **Strict Local Privacy**: Never logs typed characters, passwords, clipboard contents, window titles, or screen coordinates — only aggregate key codes, click counts, and optional foreground process names (e.g. `Code.exe`). Data is stored locally in `%LOCALAPPDATA%\TypeCount\typecount.sqlite3`.
- **System Tray & Stealth Background Mode**:
  - **System Tray Mode**: Single left-click on the tray icon opens TypeCount immediately; right-click provides quick controls (Open, Mini Mode, Enter Stealth, Pause, Auto-start, Quit).
  - **Stealth Mode (No Tray Icon)**: Runs invisibly without a tray icon while continuing to count silently. Reopen anytime using the global wake hotkey (`Ctrl+Alt+Shift+T` by default) or by relaunching `TypeCount.exe`.
- **Rich Visual Analytics**:
  - **Overview**: Today / 7-Day / Lifetime cards, Live WPM / CPM & Peak WPM, Mouse Odometer, Daily Goal & Streak, Ergonomic Insights & Strain Guard, 1m/10m/1h/24h rolling windows, 24h Hourly chart, 14/30/90-day Trend, Activity Mix, and 365-day Activity Calendar.
  - **History**: Full daily table, interactive trend, and hourly scatter/brush distribution explorer.
  - **Heatmap & Treemap**: Compact & Full-size ANSI keyboard heatmap, proportional key-usage treemap, and SVG mouse button breakdown.
  - **Applications & Focus Timer**: Per-application keyboard/mouse/scroll/active time attribution plus a built-in 25-minute Focus Timer.
  - **Typing Test, Shortcuts & Report Cards**: Integrated speed test, Windows shortcut encyclopedia & ranking, and 7 styled `1080×1350` PNG report card exports.
  - **Mini (`320×208`) + Sport Mode**: Always-on-top compact widget with live Warp/CPS mode.
- **Multilingual & Customizable**: Instant **English (`en`)** and **Persian (`fa`, full RTL + Jalali)** switching, plus 7 additional languages, 4 color palettes (`Blue`, `Teal`, `Violet`, `Graphite`), `80%–150%` UI scaling, CSV/JSON exports, rotating SQLite backups, and non-double-counting `.sqlite3` database import/merge.

### Command-Line Options

- `TypeCount.exe` — Launches and shows the management window (or focuses the already-running instance).
- `TypeCount.exe --background` — Starts silently using your saved startup mode (`tray` or `stealth`).
- `TypeCount.exe --tray` — Starts hidden with a system tray icon.
- `TypeCount.exe --stealth` — Starts hidden with no tray icon (`Ctrl+Alt+Shift+T` or relaunch to open).
- `TypeCount.exe --demo` — Opens with 180 days of sample data without touching your real database.

## License & Attribution

Licensed under **GNU GPLv3 or later** ([LICENSE](LICENSE)).
UI layout concepts, keyboard/mouse visualization components, and shortcut catalog structures are adapted from [ClickPulse](https://github.com/endNone/ClickPulse) (`GPL-3.0-or-later`), combined with original branding, artwork, installer packages, extended features, and a native C++17 Win32/WebView2 architecture.
