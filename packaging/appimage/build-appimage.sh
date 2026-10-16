#!/usr/bin/env bash
# Builds Blokkily-<version>-x86_64.AppImage from a Release build tree
# (features/packaging.feature). The AppImage carries Qt, its QML modules and
# every library the application and its scan helper link, as linuxdeploy and
# its Qt plugin find them; it still finds the plugins, SoundFonts and audio
# server installed on the machine it runs on.
#
#   packaging/appimage/build-appimage.sh <release-build-dir> [output-dir]
#
# linuxdeploy and its Qt plugin are downloaded once into $LINUXDEPLOY_TOOLS
# (default ~/.cache/blokkily-appimage) unless already there.
set -euo pipefail

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$(cd "${1:?usage: build-appimage.sh <release-build-dir> [output-dir]}" && pwd)"
output_dir="$(mkdir -p "${2:-$build_dir}" && cd "${2:-$build_dir}" && pwd)"
tools="${LINUXDEPLOY_TOOLS:-$HOME/.cache/blokkily-appimage}"
arch="$(uname -m)"
version="$(sed -n 's/^project(blokkily VERSION \([0-9.]*\).*/\1/p' "$source_dir/CMakeLists.txt")"

fetch() {
    local name="$1" url="$2"
    if [[ ! -x "$tools/$name" ]]; then
        mkdir -p "$tools"
        curl -fsSL -o "$tools/$name.part" "$url"
        chmod +x "$tools/$name.part"
        mv "$tools/$name.part" "$tools/$name"
    fi
}
fetch "linuxdeploy-$arch.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage"
fetch "linuxdeploy-plugin-qt-$arch.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$arch.AppImage"

appdir="$build_dir/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build_dir" --prefix /usr

# The Qt plugin bundles the QML modules the window imports.
export QML_SOURCES_PATHS="$source_dir/ui"
export EXTRA_QT_MODULES="svg"
export PATH="$tools:$PATH"
# Runs where FUSE is not available too (containers, CI).
export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_OUTPUT="$output_dir/Blokkily-$version-$arch.AppImage"
export OUTPUT="$LDAI_OUTPUT"

cd "$output_dir"
"$tools/linuxdeploy-$arch.AppImage" \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/blokkily" \
    --executable "$appdir/usr/bin/blokkily_scan" \
    --desktop-file "$appdir/usr/share/applications/blokkily.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/blokkily.svg" \
    --plugin qt \
    --output appimage
echo "$LDAI_OUTPUT"
