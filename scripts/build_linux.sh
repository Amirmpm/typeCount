#!/usr/bin/env bash
set -euo pipefail

APP_VER="$(python3 -c 'import re; print(re.search(r"TC_VERSION_STR\s+\"([^\"]+)\"", open("src/core/version.hpp").read()).group(1))')"
TAR_OUT="releases/TypeCount-v${APP_VER}-linux-x64-portable.tar.gz"
DEB_OUT="releases/TypeCount-v${APP_VER}-linux-x64-setup.deb"

echo "[1/5] Refreshing locales, icons, and UI asset bundle (v${APP_VER})..."
python3 scripts/build_locales.py
python3 scripts/make_icons.py
python3 scripts/pack_ui.py

WEBKIT_PKG="webkit2gtk-4.1"
WEBKIT_DEP="libwebkit2gtk-4.1-0"
if ! pkg-config --exists webkit2gtk-4.1 2>/dev/null; then
  WEBKIT_PKG="webkit2gtk-4.0"
  WEBKIT_DEP="libwebkit2gtk-4.0-37"
fi
echo "Using GTK+3 and $WEBKIT_PKG"

mkdir -p tests/bin/linux_obj releases

echo "[2/5] Compiling SQLite3 & UI bundle..."
gcc -O2 -DSQLITE_THREADSAFE=1 -c vendor/sqlite3/sqlite3.c -o tests/bin/linux_obj/sqlite3.o
g++ -std=c++17 -O2 -c src/win/ui_bundle.cpp -o tests/bin/linux_obj/ui_bundle.o

echo "[3/5] Compiling and linking TypeCount Linux x64..."
g++ -std=c++17 -O2 -s -Wall -Wextra \
  -Isrc/core -Isrc/linux -Ivendor/sqlite3 \
  src/linux/main.cpp src/core/db.cpp \
  tests/bin/linux_obj/sqlite3.o tests/bin/linux_obj/ui_bundle.o \
  $(pkg-config --cflags --libs gtk+-3.0 "$WEBKIT_PKG") \
  -lX11 -lXi -lXrandr -lpthread -ldl \
  -o tests/bin/linux_obj/typecount

echo "[4/5] Packaging $TAR_OUT..."
TAR_ROOT="tests/bin/linux_obj/TypeCount-v${APP_VER}-linux-x64"
rm -rf "$TAR_ROOT"
mkdir -p "$TAR_ROOT"
cp tests/bin/linux_obj/typecount "$TAR_ROOT/typecount"
cp assets/typecount-256.png "$TAR_ROOT/typecount.png"
cat > "$TAR_ROOT/typecount.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=TypeCount
Comment=Privacy-first offline keyboard & mouse analytics
Exec=typecount
Icon=typecount
Terminal=false
Categories=Utility;
StartupNotify=true
EOF

cat > "$TAR_ROOT/install.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/256x256/apps"
install -m 755 "$HERE/typecount" "$HOME/.local/bin/typecount"
install -m 644 "$HERE/typecount.png" "$HOME/.local/share/icons/hicolor/256x256/apps/typecount.png"
sed "s|^Exec=.*|Exec=$HOME/.local/bin/typecount|" "$HERE/typecount.desktop" > "$HOME/.local/share/applications/typecount.desktop"
echo "Installed TypeCount into $HOME/.local/bin/typecount"
EOF

cat > "$TAR_ROOT/uninstall.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
rm -f "$HOME/.local/bin/typecount" \
      "$HOME/.local/share/applications/typecount.desktop" \
      "$HOME/.local/share/icons/hicolor/256x256/apps/typecount.png" \
      "$HOME/.config/autostart/typecount.desktop"
echo "Removed TypeCount user installation."
EOF
chmod +x "$TAR_ROOT/typecount" "$TAR_ROOT/install.sh" "$TAR_ROOT/uninstall.sh"
tar -czf "$TAR_OUT" -C tests/bin/linux_obj "TypeCount-v${APP_VER}-linux-x64"

echo "[5/5] Packaging $DEB_OUT..."
DEB_ROOT="tests/bin/linux_obj/deb_pkg"
rm -rf "$DEB_ROOT"
mkdir -p "$DEB_ROOT/DEBIAN" \
         "$DEB_ROOT/usr/bin" \
         "$DEB_ROOT/usr/share/applications" \
         "$DEB_ROOT/usr/share/icons/hicolor/256x256/apps"

install -m 755 tests/bin/linux_obj/typecount "$DEB_ROOT/usr/bin/typecount"
install -m 644 assets/typecount-256.png "$DEB_ROOT/usr/share/icons/hicolor/256x256/apps/typecount.png"
cat > "$DEB_ROOT/usr/share/applications/typecount.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=TypeCount
Comment=Privacy-first offline keyboard & mouse analytics
Exec=/usr/bin/typecount
Icon=typecount
Terminal=false
Categories=Utility;
StartupNotify=true
EOF

cat > "$DEB_ROOT/DEBIAN/control" <<EOF
Package: typecount
Version: ${APP_VER}
Section: utils
Priority: optional
Architecture: amd64
Maintainer: TypeCount <https://github.com/Amirmpm/typeCount>
Depends: libgtk-3-0, ${WEBKIT_DEP}, libx11-6, libxi6, libxrandr2
Recommends: libnotify-bin, pulseaudio-utils | pipewire-bin | alsa-utils
Description: Privacy-first offline keyboard and mouse analytics
 TypeCount tracks keyboard shortcuts, typing cadence, mouse travel,
 per-application usage, and ergonomic break reminders 100% locally.
EOF

dpkg-deb --root-owner-group --build "$DEB_ROOT" "$DEB_OUT"
ls -lh "$TAR_OUT" "$DEB_OUT"
