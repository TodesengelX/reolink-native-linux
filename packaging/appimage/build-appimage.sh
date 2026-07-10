#!/usr/bin/env bash
# Build a self-contained AppImage using linuxdeploy + the Qt plugin.
# Requires: linuxdeploy, linuxdeploy-plugin-qt (on PATH), a Release build.
#
#   ./packaging/appimage/build-appimage.sh
#
# Produces Reolink_Linux_Client-x86_64.AppImage in the repo root.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
APPDIR="$ROOT/AppDir"

command -v linuxdeploy >/dev/null || { echo "linuxdeploy not found on PATH" >&2; exit 1; }

# Configure + build Release if needed.
if [ ! -x "$BUILD/reolink-client" ]; then
    cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD" -j"$(nproc)"
fi

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"

export QML_SOURCES_PATHS="$ROOT/src/ui/qml"
linuxdeploy --appdir "$APPDIR" \
    --desktop-file "$ROOT/packaging/io.github.todesengelx.ReolinkLinux.desktop" \
    --icon-file "$ROOT/packaging/io.github.todesengelx.ReolinkLinux.svg" \
    --plugin qt \
    --output appimage

echo "AppImage built in $ROOT"
