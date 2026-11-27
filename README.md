# TypeCount

**TypeCount** is a privacy-first, offline Windows desktop application for tracking keyboard and mouse activity, analytics, shortcuts, per-application breakdowns, typing speed, and ergonomic habits — built in native C++17 (Win32 Raw Input + Edge WebView2 + SQLite3).

## Key Features

- **Native Windows Background Capture (`WM_INPUT`)**: Counts keystrokes, mouse clicks (Left/Right/Middle/Side Back/Side Forward), scroll wheel events, mouse travel distance, and modifier shortcut chords across Windows with minimal CPU and ~3 MB idle RAM when hidden.
- **Strict Local Privacy**: Never logs typed text, clipboard contents, window titles, or mouse coordinates — only canonical key codes, click counts, and optional foreground process names (e.g. `Code.exe`). All data stays locally in `%LOCALAPPDATA%\TypeCount\typecount.sqlite3`.
- **Auto-Start, System Tray & Ultra-Stealth Mode**:
  - Automatically registers Windows login startup (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`) with `--background`.
  - **System Tray Mode**: Single left-click on the tray icon opens the management window immediately; right-click opens quick actions (Open, Mini Mode, Enter Stealth, Pause, Auto-start, Quit).
  - **Stealth Background Mode (No Tray Icon)**: Completely hides the tray icon while continuing to record silently. Reopen anytime via the customizable global hotkey (default `Ctrl+Alt+Shift+T`) or simply by re-launching `TypeCount.exe` (single-instance activation).
- **Rich Visual Analytics**:
  - **Overview**: Today / 7-Day / Lifetime cards, Live Typing Speed (`WPM` / `CPM` / peak), Mouse Odometer (scrolls & traveled meters), Daily Goal progress & active Streak, Break / 20-20-20 Reminder, Live 1m/10m/1h/24h windows, 24h Hourly chart, 14/30/90-day Trend, Activity Mix donut, and 365-day Activity Calendar.
  - **History**: Full daily table, interactive trend, and hourly scatter/brush distribution explorer.
  - **Heatmap & Treemap**: Compact & Full-size ANSI keyboard heatmap, proportional key-usage treemap, and SVG mouse button distribution.
  - **Applications & Focus Timer**: Per-application breakdown (keyboard/mouse/scroll/active time) plus a built-in 25-minute Focus Timer.
  - **Shortcuts & Report Cards**: Windows shortcut encyclopedia + ranking/search, 7 styled `1080×1350` PNG report card exports, and AI prompt generator.
  - **Mini (`320×208`) + Sport Mode**: Always-on-top mini widget with real-time Warp/CPS mode.
- **Export, Auto-Backup & Bilingual UI**:
  - Instant **English (`en`, default)** and **Persian (`fa`, full RTL)** switching, plus 7 additional languages.
  - 4 palettes (`Blue`, `Teal`, `Violet`, `Graphite`) and UI scale `80%–150%`.
  - One-click **CSV** / **JSON** export, rotating automatic SQLite backups (last 5 kept), and non-double-counting `.sqlite3` database import/merge.

## Download / Prebuilt Executable

The standalone x86_64 Windows binary (requires only Windows 10/11 Edge WebView2 Runtime, built into modern Windows) is located at:

- `releases/TypeCount.exe`

Current build: PE32+ x86-64 GUI, 2.08 MB, SHA-256 `a1f72323612d7c3beaeb6e02c6f113cc66a9e1e6451c41222eee2f64e25e30e5`, built with zig 0.17.1 — all C++ core tests and UI/i18n checks passing.

### CLI Flags

- `TypeCount.exe` — Launches and shows the management UI (or activates the already-running instance).
- `TypeCount.exe --background` — Starts silently according to your saved startup mode (`tray` or `stealth`).
- `TypeCount.exe --tray` — Starts hidden with a system tray icon.
- `TypeCount.exe --stealth` — Starts completely hidden with no tray icon (`Ctrl+Alt+Shift+T` or relaunch to open).
- `TypeCount.exe --demo` — Opens with 180 days of synthetic sample data without modifying real statistics.

## Building & Testing from Source

Requires Python 3.11+, `zig` (e.g. `pip install ziglang`), and optionally `g++` + `node` for tests:

```bash
# 1. Run platform-independent C++ core unit tests
mkdir -p tests/bin
gcc -O2 -DSQLITE_THREADSAFE=1 -c vendor/sqlite3/sqlite3.c -o tests/bin/sqlite3.o
g++ -std=c++17 -O2 -Wall -Wextra -Isrc/core -Ivendor/sqlite3 \
  tests/test_core.cpp src/core/db.cpp tests/bin/sqlite3.o \
  -lpthread -ldl -o tests/bin/test_core
./tests/bin/test_core

# 2. Run UI & i18n checks
node tests/test_ui.cjs

# 3. Cross-compile standalone releases/TypeCount.exe (x86_64 Windows PE32+)
bash scripts/build_windows.sh
```

## License & Attribution

Licensed under **GNU GPLv3 or later** ([LICENSE](LICENSE)).
UI styling, keyboard/mouse layout data, and shortcuts catalog are adapted from [ClickPulse](https://github.com/endNone/ClickPulse) (`GPL-3.0-or-later`), with distinct branding, original icon artwork (`assets/`), and a new C++17 Win32/WebView2 architecture.
