#!/usr/bin/env bash
set -euo pipefail

ZIG_BIN="${ZIG_BIN:-/tmp/pydeps/ziglang/zig}"
if ! command -v "$ZIG_BIN" >/dev/null 2>&1; then
  ZIG_BIN="zig"
fi

APP_VER="$(python3 -c 'import re; print(re.search(r"TC_VERSION_STR\s+\"([^\"]+)\"", open("src/core/version.hpp").read()).group(1))')"
PORTABLE_OUT="releases/TypeCount-v${APP_VER}-windows-x64-portable.exe"
SETUP_OUT="releases/TypeCount-v${APP_VER}-windows-x64-setup.exe"

echo "[1/7] Refreshing locales, generating icons, and packing UI (v${APP_VER})..."
python3 scripts/build_locales.py
python3 scripts/make_icons.py
python3 scripts/pack_ui.py

mkdir -p tests/bin/win_obj releases

echo "[2/7] Compiling Windows app resources (TypeCount.ico, manifest, version)..."
"$ZIG_BIN" rc /:output-format coff /:target x86_64 /fo tests/bin/win_obj/app.res.o assets/app.rc

echo "[3/7] Compiling SQLite3 for Windows x86_64..."
"$ZIG_BIN" cc -target x86_64-windows-gnu -Os -DSQLITE_THREADSAFE=1 -c vendor/sqlite3/sqlite3.c -o tests/bin/win_obj/sqlite3.o

echo "[4/7] Compiling embedded UI asset bundle..."
"$ZIG_BIN" c++ -target x86_64-windows-gnu -std=c++17 -Os -c src/win/ui_bundle.cpp -o tests/bin/win_obj/ui_bundle.o

echo "[5/7] Compiling and linking $PORTABLE_OUT..."
"$ZIG_BIN" c++ -target x86_64-windows-gnu -std=c++17 -Os -s \
  -DUNICODE -D_UNICODE \
  -Isrc/core -Isrc/win -Ivendor/sqlite3 -Ivendor/webview2 -Ivendor/webview \
  src/win/main.cpp src/core/db.cpp \
  tests/bin/win_obj/sqlite3.o tests/bin/win_obj/ui_bundle.o tests/bin/win_obj/app.res.o \
  -o tests/bin/win_obj/TypeCount.exe \
  -Wl,--subsystem,windows \
  -lole32 -luuid -lshlwapi -lversion -lshell32 -luser32 -lgdi32 -ladvapi32 -ldwmapi -lcomdlg32 -lwinhttp -lwinmm -lmsi -municode

cp tests/bin/win_obj/TypeCount.exe "$PORTABLE_OUT"

echo "[6/7] Compiling Windows Setup Wizard resources..."
"$ZIG_BIN" rc /:output-format coff /:target x86_64 /fo tests/bin/win_obj/setup.res.o assets/setup.rc

echo "[7/7] Compiling and linking $SETUP_OUT..."
"$ZIG_BIN" c++ -target x86_64-windows-gnu -std=c++17 -Os -s \
  -DUNICODE -D_UNICODE \
  -Isrc/core \
  src/installer/setup_win32.cpp tests/bin/win_obj/setup.res.o \
  -o "$SETUP_OUT" \
  -Wl,--subsystem,windows \
  -lcomctl32 -lmsi -lole32 -luuid -lshell32 -lshlwapi -lversion -ladvapi32 -luser32 -lgdi32 -municode

ls -lh "$PORTABLE_OUT" "$SETUP_OUT"
