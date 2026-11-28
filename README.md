# TypeCount

**TypeCount** is a privacy-first, offline cross-platform desktop application (**Windows**, **macOS**, and **Linux**) for tracking keyboard and mouse activity, typing speed, ergonomic wellness, shortcuts, and per-application breakdowns — built with native OS input capture (`WM_INPUT`, `CGEventTap`, `XInput2` / `evdev`), native WebView hosts (`Edge WebView2`, `WKWebView`, `WebKit2GTK`), and local SQLite storage.

## Download (`v1.2.0`)

Get the latest packages for **Windows**, **macOS**, and **Linux** from the **[GitHub Releases](https://github.com/Amirmpm/typeCount/releases/latest)** page:

| Platform | Package | Best For | Details |
|---|---|---|---|
| **Windows (x64)** | **`TypeCount-v1.2.0-windows-x64-setup.msi`** | Standard Windows Installer | MSI setup with in-place upgrade detection, Start Menu & Desktop shortcuts, and Apps & Features uninstaller |
| **Windows (x64)** | **`TypeCount-v1.2.0-windows-x64-setup.exe`** | Interactive Setup Wizard (EN / فارسی) | DPI-scaled 4-step bilingual setup wizard with OS dark mode, `< Back` navigation, in-place upgrades, and clean uninstaller |
| **Windows (x64)** | **`TypeCount-v1.2.0-windows-x64-portable.exe`** | Portable Executable | Single standalone binary for Windows 10/11 (x64) with zero installation required |
| **macOS (Universal)** | **`TypeCount-v1.2.0-macos-universal-setup.dmg`** | macOS Installer Disk Image | Drag-to-Applications `.dmg` containing Universal (`arm64` Apple Silicon + `x86_64` Intel) `TypeCount.app` |
| **macOS (Universal)** | **`TypeCount-v1.2.0-macos-universal-portable.zip`** | Portable App Bundle | Signed Universal `TypeCount.app` archive for macOS 11+ |
| **Linux (x64)** | **`TypeCount-v1.2.0-linux-x64-setup.deb`** | Debian / Ubuntu Package | System-wide install (`/usr/bin/typecount`, desktop launcher, hicolor icon) |
| **Linux (x64)** | **`TypeCount-v1.2.0-linux-x64-portable.tar.gz`** | Portable Archive | Standalone `typecount` binary with optional user `install.sh` / `uninstall.sh` scripts |

> **Requirements:** Windows 10/11 (`x64` with built-in Edge WebView2), macOS 11+ (`arm64` / `x86_64` with Accessibility / Input Monitoring permission), or Linux (`x64` with GTK3 + WebKit2GTK; X11 XInput2 or `input` group evdev).

## What's New in v1.2.0

- **Cross-Platform Native Desktop Builds (Windows, macOS & Linux)**:
  - **macOS (`src/macos/main.mm`)**: Native Cocoa + `WKWebView` + `CGEventTap` + `NSSound` + LaunchAgent auto-start, compiled as a Universal (`arm64` + `x86_64`) app bundle (`.dmg` & `.zip`).
  - **Linux (`src/linux/`)**: Native GTK3 + `WebKit2GTK` + `XInput2` global capture (with `/dev/input/event*` fallback), XDG autostart, desktop notifications, and `.deb` / `.tar.gz` packages.
- **Full Light & Dark Mode**: Automatic OS dark mode sync (`System default`), explicit `Light` and `Dark` theme options, dark-adapted color palettes (`Blue`, `Teal`, `Violet`, `Graphite`), native DWM / GTK / Cocoa window styling with zero startup flash, and dark-mode-aware SVG heatmaps, charts, and scatter plots.
- **Automatic Monitor Physical PPI Detection**: Automatically detects physical display pixel density (`GetDpiForMonitor` / `GetDeviceCaps` on Windows, `CGDisplayScreenSize` Quartz points on macOS, `GdkMonitor` physical dimensions on Linux) for accurate mouse travel distance in meters — with manual override (`72–360 PPI`) whenever preferred.
- **Soft Pastel Notification Chimes**: Built-in C++17 44.1 kHz PCM WAV synthesizer with 6 gentle acoustic chime presets (`Pastel Pearl`, `Morning Mist`, `Peach Marimba`, `Lavender Harp`, `Mint Dewdrop`, `Silk Breeze`) plus `Silent` and instant in-settings `Preview`.
- **Refreshed Windows Setup Wizard & Standardized Release Naming**: Multi-step `< Back` wizard, per-monitor DPI layout scaling, OS dark theme integration, and consistent `<App>-v<Version>-<os>-<arch>-<portable|setup>.<ext>` package names across every platform.

## Key Features

- **Native Background Input Capture**: Counts keystrokes, mouse clicks (Left / Right / Middle / Side Back / Side Forward), scroll wheel steps, cursor travel distance, and modifier shortcut chords with minimal CPU and memory overhead.
- **Strict Local Privacy**: Never logs typed characters, passwords, clipboard contents, window titles, or screen coordinates — only aggregate key codes, click counts, and optional foreground application names stored locally in `typecount.sqlite3`.
- **System Tray / Menu Bar & Stealth Background Mode**:
  - **Tray / Menu Bar Mode**: Quick access to Open, Mini Mode, Stealth Mode, Pause/Resume, and Save & Quit.
  - **Stealth Mode (No Tray Icon)**: Runs silently in the background while counting. Reopen anytime using the global wake hotkey (`Ctrl+Alt+Shift+T` by default) or by relaunching TypeCount.
- **Rich Visual Analytics**:
  - **Overview**: Today / 7-Day / Lifetime cards, Live WPM / CPM & Peak WPM, Mouse Odometer, Daily Goal & Streak, Ergonomic Insights & Strain Guard, 1m/10m/1h/24h rolling windows, 24h Hourly chart, 14/30/90-day Trend, Activity Mix, and 365-day Activity Calendar.
  - **History**: Full daily table, interactive trend, and hourly scatter/brush distribution explorer.
  - **Heatmap & Treemap**: Compact & Full-size ANSI keyboard heatmap, proportional key-usage treemap, and SVG mouse button breakdown.
  - **Applications & Focus Timer**: Per-application keyboard/mouse/scroll/active time attribution plus a built-in 25-minute Focus Timer.
  - **Typing Test, Shortcuts & Report Cards**: Integrated speed test (EN / فارسی / Code), platform-aware shortcut atlas & ranking, and 7 styled `1080×1350` PNG report card exports.
  - **Mini (`320×208`) + Sport Mode**: Always-on-top compact widget with live CPS/WPM mode.
- **Multilingual & Customizable**: Instant **English (`en`)** and **Persian (`fa`, full RTL + Jalali / شمسی)** switching, plus 7 additional languages, Light & Dark modes, 4 color palettes, `80%–150%` UI scaling, CSV/JSON exports, rotating SQLite backups, and non-double-counting `.sqlite3` database merge.

### Command-Line Options

- `--background` — Starts silently using your saved startup mode (`tray` or `stealth`).
- `--tray` — Starts hidden with a system tray / menu bar icon.
- `--stealth` — Starts hidden with no tray icon (wake hotkey or relaunch to open).
- `--demo` — Opens with 180 days of sample data without touching your real database.

## Building from Source

- **Windows (`x64`)**: `bash scripts/build_windows.sh` (cross-compiles via Zig + WiX v5 for `.msi`)
- **Linux (`x64`)**: `bash scripts/build_linux.sh` (requires `gtk+-3.0`, `webkit2gtk-4.1` or `4.0`, `libXi`, `libXrandr`)
- **macOS (`Universal arm64 + x86_64`)**: `bash scripts/build_macos.sh` (requires Xcode Command Line Tools on macOS)

## License & Attribution

Licensed under **GNU GPLv3 or later** ([LICENSE](LICENSE)).
UI layout concepts, keyboard/mouse visualization components, and shortcut catalog structures are adapted from [ClickPulse](https://github.com/endNone/ClickPulse) (`GPL-3.0-or-later`), combined with original branding, artwork, native installers, cross-platform C++17 / Objective-C++ hosts, synthesized pastel audio alerts, and extended analytics features.
