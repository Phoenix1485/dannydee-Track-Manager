#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${BUILD_DIR:-$project_root/build-macos}"
dist_dir="${DIST_DIR:-$project_root/dist}"
version="$(sed -nE 's/^project\(DannyDeeTrackManager VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$project_root/CMakeLists.txt" | head -n 1)"
if [[ -z "$version" ]]; then
    echo "The project version could not be read from CMakeLists.txt." >&2
    exit 1
fi

qt_root="${QT_ROOT:-}"
if [[ -z "$qt_root" ]] && command -v brew >/dev/null 2>&1; then
    qt_root="$(brew --prefix qt 2>/dev/null || true)"
fi
if [[ -z "$qt_root" || ! -x "$qt_root/bin/macdeployqt" ]]; then
    echo "Qt 6 was not found. Set QT_ROOT to the Qt installation directory." >&2
    exit 1
fi

cmake_args=(
    -S "$project_root"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_PREFIX_PATH=$qt_root"
)
if [[ -n "${CMAKE_OSX_ARCHITECTURES:-}" ]]; then
    cmake_args+=("-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
fi
if [[ -n "${UPDATE_MANIFEST_URL:-}" ]]; then
    cmake_args+=("-DDANNYDEE_UPDATE_MANIFEST_URL=${UPDATE_MANIFEST_URL}")
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --config Release

app="$build_dir/DannyDeeTrackManager.app"
if [[ ! -d "$app" ]]; then
    echo "Application bundle was not produced: $app" >&2
    exit 1
fi

"$qt_root/bin/macdeployqt" "$app" -always-overwrite
tools_dir="$app/Contents/Resources/tools"
mkdir -p "$tools_dir" "$dist_dir"

icon_work="$(mktemp -d)"
trap 'rm -rf "$icon_work"' EXIT
qlmanage -t -s 1024 -o "$icon_work" "$project_root/resources/app-icon.svg" >/dev/null 2>&1
icon_source="$icon_work/app-icon.svg.png"
if [[ -f "$icon_source" ]]; then
    iconset="$icon_work/app.iconset"
    mkdir -p "$iconset"
    while read -r filename pixels; do
        sips -z "$pixels" "$pixels" "$icon_source" --out "$iconset/$filename" >/dev/null
    done <<'SIZES'
icon_16x16.png 16
icon_16x16@2x.png 32
icon_32x32.png 32
icon_32x32@2x.png 64
icon_128x128.png 128
icon_128x128@2x.png 256
icon_256x256.png 256
icon_256x256@2x.png 512
icon_512x512.png 512
icon_512x512@2x.png 1024
SIZES
    iconutil -c icns "$iconset" -o "$app/Contents/Resources/app.icns"
else
    echo "Warning: the macOS application icon could not be generated." >&2
fi

copy_tool() {
    local target_name="$1"
    local explicit_path="$2"
    local command_name="$3"
    local source_path="$explicit_path"
    if [[ -z "$source_path" ]]; then
        source_path="$(command -v "$command_name" 2>/dev/null || true)"
    fi
    if [[ -n "$source_path" && -f "$source_path" ]]; then
        cp -L "$source_path" "$tools_dir/$target_name"
        chmod 755 "$tools_dir/$target_name"
        echo "Bundled $target_name"
    else
        echo "Warning: $target_name was not bundled; set its *_PATH variable or install it in PATH." >&2
    fi
}

copy_tool "ffmpeg" "${FFMPEG_PATH:-}" "ffmpeg"
copy_tool "ffprobe" "${FFPROBE_PATH:-}" "ffprobe"
copy_tool "yt-dlp" "${YTDLP_PATH:-}" "yt-dlp"
copy_tool "spotdl" "${SPOTDL_PATH:-}" "spotdl"
copy_tool "deno" "${DENO_PATH:-}" "deno"

cp "$project_root/README.md" "$app/Contents/Resources/README.md"
cp "$project_root/THIRD_PARTY_NOTICES.txt" "$app/Contents/Resources/THIRD_PARTY_NOTICES.txt"

signing_identity="${CODESIGN_IDENTITY:--}"
find "$tools_dir" -type f -perm -111 -exec codesign --force --sign "$signing_identity" {} \;
codesign --force --deep --sign "$signing_identity" "$app"

dmg="$dist_dir/DannyDee-Track-Manager-$version-macOS.dmg"
rm -f "$dmg"
hdiutil create -volname "DannyDee Track Manager" -srcfolder "$app" -ov -format UDZO "$dmg"
shasum -a 256 "$dmg" | tee "$dmg.sha256"
echo "Created $dmg"
