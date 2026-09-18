#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build-macos}"
DIST_DIR="${DIST_DIR:-$PROJECT_ROOT/dist}"
QT_ROOT="${QT_ROOT:-}"
UPDATE_MANIFEST_URL="${UPDATE_MANIFEST_URL:-}"
MACOS_PLATFORM_KEY="${MACOS_PLATFORM_KEY:-macos-arm64}"

if [[ -z "$QT_ROOT" || ! -d "$QT_ROOT" ]]; then
    echo "QT_ROOT must point to a Qt macOS installation." >&2
    exit 1
fi
if [[ "$MACOS_PLATFORM_KEY" != "macos-arm64" && "$MACOS_PLATFORM_KEY" != "macos-x64" ]]; then
    echo "MACOS_PLATFORM_KEY must be macos-arm64 or macos-x64." >&2
    exit 1
fi

case "$MACOS_PLATFORM_KEY" in
    macos-arm64) PLATFORM_ARCH="arm64" ;;
    macos-x64) PLATFORM_ARCH="x86_64" ;;
esac

VERSION="$(sed -nE 's/^project\(DannyDeeTrackManager VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$PROJECT_ROOT/CMakeLists.txt" | head -n 1)"
if [[ -z "$VERSION" ]]; then
    echo "Could not read the application version from CMakeLists.txt." >&2
    exit 1
fi

for variable in FFMPEG_PATH FFPROBE_PATH YTDLP_PATH SPOTDL_PATH DENO_PATH; do
    value="${!variable:-}"
    if [[ -z "$value" || ! -f "$value" ]]; then
        echo "$variable must point to an existing executable." >&2
        exit 1
    fi
done

if [[ "${CLEAN:-0}" == "1" && -d "$BUILD_DIR" ]]; then
    case "$BUILD_DIR" in
        "$PROJECT_ROOT"/build-macos|"$PROJECT_ROOT"/build-macos/*) rm -rf "$BUILD_DIR" ;;
        *) echo "Refusing to remove unsafe build path: $BUILD_DIR" >&2; exit 1 ;;
    esac
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_ROOT" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DCMAKE_OSX_ARCHITECTURES="$PLATFORM_ARCH" \
    -DDANNYDEE_UPDATE_MANIFEST_URL="$UPDATE_MANIFEST_URL" \
    -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --parallel
ctest --test-dir "$BUILD_DIR" --output-on-failure

APP_PATH="$BUILD_DIR/DannyDeeTrackManager.app"
if [[ ! -d "$APP_PATH" ]]; then
    echo "The macOS application bundle was not produced." >&2
    exit 1
fi

"$QT_ROOT/bin/macdeployqt" "$APP_PATH" -always-overwrite -no-codesign

RESOURCES_DIR="$APP_PATH/Contents/Resources"
TOOLS_DIR="$RESOURCES_DIR/tools"
DEPENDENCY_DIR="$APP_PATH/Contents/Frameworks/ffmpeg"
mkdir -p "$TOOLS_DIR" "$DEPENDENCY_DIR" "$DIST_DIR"
cp "$PROJECT_ROOT/README.md" "$RESOURCES_DIR/README.md"
cp "$PROJECT_ROOT/THIRD_PARTY_NOTICES.txt" "$RESOURCES_DIR/THIRD_PARTY_NOTICES.txt"

is_system_dependency() {
    case "$1" in
        /System/Library/*|/usr/lib/*|@rpath/*|@loader_path/*|@executable_path/*) return 0 ;;
        *) return 1 ;;
    esac
}

copy_dependency() {
    local source="$1"
    local base destination dependency dependency_base
    base="$(basename "$source")"
    destination="$DEPENDENCY_DIR/$base"
    if [[ -f "$destination" ]]; then
        return
    fi
    if [[ ! -f "$source" ]]; then
        echo "Missing dynamic dependency: $source" >&2
        exit 1
    fi
    cp -L "$source" "$destination"
    chmod u+w "$destination"
    lipo -verify_arch "$PLATFORM_ARCH" "$destination"
    install_name_tool -id "@loader_path/$base" "$destination" 2>/dev/null || true
    while IFS= read -r dependency; do
        [[ -z "$dependency" ]] && continue
        is_system_dependency "$dependency" && continue
        copy_dependency "$dependency"
        dependency_base="$(basename "$dependency")"
        install_name_tool -change "$dependency" "@loader_path/$dependency_base" "$destination"
    done < <(otool -L "$source" | tail -n +2 | awk '{print $1}')
}

bundle_tool() {
    local source="$1"
    local name="$2"
    local destination dependency dependency_base
    destination="$TOOLS_DIR/$name"
    cp -L "$source" "$destination"
    chmod u+w,+x "$destination"
    if ! file "$destination" | grep -q 'Mach-O'; then
        echo "Bundled tool is not a Mach-O executable: $source" >&2
        exit 1
    fi
    lipo -verify_arch "$PLATFORM_ARCH" "$destination"
    while IFS= read -r dependency; do
        [[ -z "$dependency" ]] && continue
        is_system_dependency "$dependency" && continue
        copy_dependency "$dependency"
        dependency_base="$(basename "$dependency")"
        install_name_tool -change "$dependency" \
            "@executable_path/../../Frameworks/ffmpeg/$dependency_base" "$destination"
    done < <(otool -L "$source" | tail -n +2 | awk '{print $1}')
}

bundle_tool "$FFMPEG_PATH" ffmpeg
bundle_tool "$FFPROBE_PATH" ffprobe
bundle_tool "$YTDLP_PATH" yt-dlp
bundle_tool "$SPOTDL_PATH" spotdl
bundle_tool "$DENO_PATH" deno

"$TOOLS_DIR/ffmpeg" -version >/dev/null
"$TOOLS_DIR/ffprobe" -version >/dev/null
"$TOOLS_DIR/yt-dlp" --version >/dev/null
"$TOOLS_DIR/spotdl" --version >/dev/null
"$TOOLS_DIR/deno" --version >/dev/null

# Add an app icon without requiring an additional image-conversion package.
ICON_WORK="$BUILD_DIR/icon-work"
mkdir -p "$ICON_WORK"
if qlmanage -t -s 1024 -o "$ICON_WORK" "$PROJECT_ROOT/resources/app-icon.svg" >/dev/null 2>&1; then
    SOURCE_ICON="$ICON_WORK/app-icon.svg.png"
    if [[ -f "$SOURCE_ICON" ]]; then
        ICONSET="$ICON_WORK/app.iconset"
        mkdir -p "$ICONSET"
        for size in 16 32 128 256 512; do
            sips -z "$size" "$size" "$SOURCE_ICON" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
            double=$((size * 2))
            sips -z "$double" "$double" "$SOURCE_ICON" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
        done
        iconutil -c icns "$ICONSET" -o "$RESOURCES_DIR/app.icns"
    fi
fi

chmod -R u+w "$APP_PATH"
codesign --force --deep --sign - "$APP_PATH"
codesign --verify --deep --strict --verbose=2 "$APP_PATH"

MAIN_EXECUTABLE="$APP_PATH/Contents/MacOS/DannyDeeTrackManager"
lipo -verify_arch "$PLATFORM_ARCH" "$MAIN_EXECUTABLE"

BAD_DEPENDENCIES=""
while IFS= read -r executable; do
    if file "$executable" | grep -q 'Mach-O'; then
        matches="$(otool -L "$executable" | grep -E '/(opt/homebrew|usr/local/(Cellar|opt))/' || true)"
        if [[ -n "$matches" ]]; then
            BAD_DEPENDENCIES+="$executable: $matches"$'\n'
        fi
    fi
done < <(find "$APP_PATH" -type f -perm -111)
if [[ -n "$BAD_DEPENDENCIES" ]]; then
    printf '%s' "$BAD_DEPENDENCIES" >&2
    echo "The app bundle still contains Homebrew dependency paths." >&2
    exit 1
fi

ARCH_LABEL="${MACOS_PLATFORM_KEY#macos-}"
DMG_NAME="DannyDee-Track-Manager-$VERSION-macOS-$ARCH_LABEL.dmg"
DMG_PATH="$DIST_DIR/$DMG_NAME"
DMG_ROOT="$BUILD_DIR/dmg-root"
rm -rf "$DMG_ROOT"
mkdir -p "$DMG_ROOT"
cp -R "$APP_PATH" "$DMG_ROOT/DannyDee Track Manager.app"
ln -s /Applications "$DMG_ROOT/Applications"
rm -f "$DMG_PATH"
hdiutil create -volname "DannyDee Track Manager $VERSION" \
    -srcfolder "$DMG_ROOT" -ov -format UDZO "$DMG_PATH"

shasum -a 256 "$DMG_PATH"
echo "macOS package created: $DMG_PATH"
