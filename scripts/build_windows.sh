#!/usr/bin/env bash
set -euo pipefail

ZIG_BIN="${ZIG_BIN:-/tmp/pydeps/ziglang/zig}"
if ! command -v "$ZIG_BIN" >/dev/null 2>&1; then
  ZIG_BIN="zig"
fi

echo "[1/5] Generating icons and packing UI..."
python3 scripts/make_icons.py
python3 scripts/pack_ui.py

mkdir -p tests/bin/win_obj releases

echo "[2/5] Compiling Windows resources (TypeCount.ico, manifest, version)..."
"$ZIG_BIN" rc /:output-format coff /:target x86_64 /fo tests/bin/win_obj/app.res.o assets/app.rc

echo "[3/5] Compiling SQLite3 for Windows x86_64..."
"$ZIG_BIN" cc -target x86_64-windows-gnu -Os -DSQLITE_THREADSAFE=1 -c vendor/sqlite3/sqlite3.c -o tests/bin/win_obj/sqlite3.o

echo "[4/5] Compiling embedded UI asset bundle..."
"$ZIG_BIN" c++ -target x86_64-windows-gnu -std=c++17 -Os -c src/win/ui_bundle.cpp -o tests/bin/win_obj/ui_bundle.o

echo "[5/5] Compiling and linking releases/TypeCount.exe..."
"$ZIG_BIN" c++ -target x86_64-windows-gnu -std=c++17 -Os -s \
  -DUNICODE -D_UNICODE \
  -Isrc/core -Isrc/win -Ivendor/sqlite3 -Ivendor/webview2 -Ivendor/webview \
  src/win/main.cpp src/core/db.cpp \
  tests/bin/win_obj/sqlite3.o tests/bin/win_obj/ui_bundle.o tests/bin/win_obj/app.res.o \
  -o releases/TypeCount.exe \
  -Wl,--subsystem,windows \
  -lole32 -luuid -lshlwapi -lversion -lshell32 -luser32 -lgdi32 -ladvapi32 -ldwmapi -lcomdlg32 -municode

ls -lh releases/TypeCount.exe
