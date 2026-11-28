#!/usr/bin/env bash
set -euo pipefail

APP_VER="$(python3 -c 'import re; print(re.search(r"TC_VERSION_STR\s+\"([^\"]+)\"", open("src/core/version.hpp").read()).group(1))')"
ZIP_OUT="releases/TypeCount-v${APP_VER}-macos-universal-portable.zip"
DMG_OUT="releases/TypeCount-v${APP_VER}-macos-universal-setup.dmg"

echo "[1/5] Refreshing locales, icons, and UI asset bundle (v${APP_VER})..."
python3 scripts/build_locales.py
python3 scripts/make_icons.py
python3 scripts/pack_ui.py

mkdir -p tests/bin/macos_obj releases

ARCH_FLAGS="-arch arm64 -arch x86_64 -mmacosx-version-min=11.0"

echo "[2/5] Compiling SQLite3, core Db, and UI bundle (Universal arm64 + x86_64)..."
clang $ARCH_FLAGS -O2 -DSQLITE_THREADSAFE=1 -c vendor/sqlite3/sqlite3.c -o tests/bin/macos_obj/sqlite3.o
clang++ $ARCH_FLAGS -std=c++17 -O2 -c src/win/ui_bundle.cpp -o tests/bin/macos_obj/ui_bundle.o
clang++ $ARCH_FLAGS -std=c++17 -O2 -Isrc/core -Ivendor/sqlite3 -c src/core/db.cpp -o tests/bin/macos_obj/db.o

echo "[3/5] Compiling & assembling TypeCount.app (Universal arm64 + x86_64)..."
APP_BUNDLE="tests/bin/macos_obj/TypeCount.app"
rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS" "$APP_BUNDLE/Contents/Resources"

clang++ $ARCH_FLAGS -std=c++17 -O2 -fobjc-arc -Wall -Wextra \
  -Isrc/core -Ivendor/sqlite3 \
  src/macos/main.mm \
  tests/bin/macos_obj/db.o tests/bin/macos_obj/sqlite3.o tests/bin/macos_obj/ui_bundle.o \
  -framework Cocoa -framework WebKit -framework CoreGraphics -framework ApplicationServices -framework UserNotifications \
  -o "$APP_BUNDLE/Contents/MacOS/TypeCount"

cp assets/typecount.icns "$APP_BUNDLE/Contents/Resources/TypeCount.icns"

cat > "$APP_BUNDLE/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>TypeCount</string>
  <key>CFBundleIconFile</key>
  <string>TypeCount.icns</string>
  <key>CFBundleIdentifier</key>
  <string>io.typecount.app</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>TypeCount</string>
  <key>CFBundleDisplayName</key>
  <string>TypeCount</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>${APP_VER}</string>
  <key>CFBundleVersion</key>
  <string>${APP_VER}</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key>
  <true/>
  <key>NSInputMonitoringUsageDescription</key>
  <string>TypeCount counts anonymous keyboard and mouse activity totals locally without logging text.</string>
</dict>
</plist>
EOF

codesign --force --deep --sign - "$APP_BUNDLE"

echo "[4/5] Packaging $ZIP_OUT..."
rm -f "$ZIP_OUT"
ditto -c -k --keepParent "$APP_BUNDLE" "$ZIP_OUT"

echo "[5/5] Packaging $DMG_OUT..."
DMG_STAGE="tests/bin/macos_obj/dmg_stage"
rm -rf "$DMG_STAGE" "$DMG_OUT"
mkdir -p "$DMG_STAGE"
cp -R "$APP_BUNDLE" "$DMG_STAGE/TypeCount.app"
ln -s /Applications "$DMG_STAGE/Applications"
hdiutil create -volname "TypeCount ${APP_VER}" -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG_OUT"

file "$APP_BUNDLE/Contents/MacOS/TypeCount"
ls -lh "$ZIP_OUT" "$DMG_OUT"
