#!/usr/bin/env bash
set -euo pipefail

# Self-contained macOS .app bundler for sfd_tool
#
# Usage (from repo root):
#   bash scripts/build_macos_app.sh
#
# This script:
#   - Ensures a Release build exists in build_cmake/
#   - Creates dist/SFD\ Tool.app with binary + resources
#   - Copies the zh_CN .mo file into Contents/Resources/locale
#   - Generates SFDTool.icns (if possible) and Info.plist
#   - Bundles Homebrew-based dylibs into Contents/Frameworks and
#     rewrites library paths so the app can run on a machine
#     without separate `brew install gtk+3 libusb gettext`

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OS_NAME="$(uname -s 2>/dev/null || echo unknown)"
if [[ "$OS_NAME" != "Darwin" ]]; then
  echo "[macos-app] Error: this script is intended to run on macOS (Darwin)." >&2
  exit 1
fi

BUILD_DIR="build_cmake"
APP_NAME="SFD Tool.app"
DIST_DIR="$ROOT_DIR/dist"
APP_ROOT="$DIST_DIR/$APP_NAME"
APP_MACOS="$APP_ROOT/Contents/MacOS"
APP_RES="$APP_ROOT/Contents/Resources"
APP_FW="$APP_ROOT/Contents/Frameworks"

# 始终从干净的构建/打包目录开始，避免权限残留或陈旧文件导致奇怪问题
rm -rf "$BUILD_DIR" "$DIST_DIR"
mkdir -p "$DIST_DIR"

# 1. Ensure we have a Release build available
if [[ ! -x "$BUILD_DIR/sfd_tool" ]]; then
  echo "[macos-app] build_cmake/sfd_tool not found, invoking scripts/release.sh..."
  if [[ -x "./scripts/release.sh" ]]; then
    ./scripts/release.sh
  else
    echo "[macos-app] Error: scripts/release.sh not found; please build Release manually (e.g. via CMake) before running this script." >&2
    exit 1
  fi
fi

if [[ ! -x "$BUILD_DIR/sfd_tool" ]]; then
  echo "[macos-app] Error: still cannot find $BUILD_DIR/sfd_tool after building." >&2
  exit 1
fi

# 2. Create basic .app structure and copy core resources
rm -rf "$APP_ROOT"
mkdir -p "$APP_MACOS" "$APP_RES/locale/zh_CN/LC_MESSAGES" "$APP_FW"

BIN="$APP_MACOS/sfd_tool"
cp "$BUILD_DIR/sfd_tool" "$BIN"
chmod +x "$BIN" || true

# Version log for About page
if [[ -f "docs/VERSION_LOG.md" ]]; then
  cp "docs/VERSION_LOG.md" "$APP_RES/VERSION_LOG.md"
fi

# zh_CN locale
MO_BUILD="$BUILD_DIR/locale/zh_CN/LC_MESSAGES/sfd_tool.mo"
if [[ -f "$MO_BUILD" ]]; then
  cp "$MO_BUILD" "$APP_RES/locale/zh_CN/LC_MESSAGES/sfd_tool.mo"
else
  echo "[macos-app] Warning: $MO_BUILD not found; About/i18n may fall back to system paths." >&2
fi

# 3. Generate or reuse SFDTool.icns
ICNS_SRC="$DIST_DIR/SFDTool.icns"
if [[ ! -f "$ICNS_SRC" ]]; then
  if command -v sips >/dev/null 2>&1 && command -v iconutil >/dev/null 2>&1; then
    ICON_PNG="assets/icon.png"
    if [[ -f "$ICON_PNG" ]]; then
      ICONSET_DIR="$DIST_DIR/SFDTool.iconset"
      rm -rf "$ICONSET_DIR"
      mkdir -p "$ICONSET_DIR"

      for size in 16 32 64 128 256 512; do
        sips -z "$size" "$size" "$ICON_PNG" --out "$ICONSET_DIR/icon_${size}x${size}.png" >/dev/null
        retina=$((size * 2))
        sips -z "$retina" "$retina" "$ICON_PNG" --out "$ICONSET_DIR/icon_${size}x${size}@2x.png" >/dev/null
      done

      iconutil -c icns "$ICONSET_DIR" -o "$ICNS_SRC" || echo "[macos-app] Warning: iconutil failed, continuing without .icns" >&2
    else
      echo "[macos-app] Warning: assets/icon.png not found, skipping .icns generation" >&2
    fi
  else
    echo "[macos-app] Warning: sips/iconutil not available, skipping .icns generation" >&2
  fi
fi

if [[ -f "$ICNS_SRC" ]]; then
  cp "$ICNS_SRC" "$APP_RES/SFDTool.icns"
fi

# 4. Generate Info.plist from template
PLIST_TEMPLATE="$ROOT_DIR/packaging/macos/Info.plist.in"
PLIST_OUT="$APP_ROOT/Contents/Info.plist"
if [[ -f "$PLIST_TEMPLATE" ]]; then
  VERSION_STR="0.0.0"
  if [[ -f "VERSION.txt" ]]; then
    VERSION_STR="$(head -n1 VERSION.txt | tr -d '[:space:]')"
  elif [[ -f "version.h" ]]; then
    # 提取 #define SFD_TOOL_VERSION "x.y.z"
    if grep -q '^#define[[:space:]]\+SFD_TOOL_VERSION' version.h 2>/dev/null; then
      VERSION_STR="$(grep '^#define[[:space:]]\+SFD_TOOL_VERSION' version.h | sed 's/.*"\(.*\)".*/\1/')"
    fi
  fi

  sed "s/@SFD_TOOL_VERSION_STR@/$VERSION_STR/g" "$PLIST_TEMPLATE" > "$PLIST_OUT"
else
  echo "[macos-app] Warning: $PLIST_TEMPLATE not found, Info.plist will be missing" >&2
fi

# 5. Bundle Homebrew-based dylibs into Contents/Frameworks
if ! command -v otool >/dev/null 2>&1 || ! command -v install_name_tool >/dev/null 2>&1; then
  echo "[macos-app] Warning: otool/install_name_tool not available, cannot rewrite dylib paths; app may still depend on system/Homebrew libs." >&2
else
  BREW_PREFIX="$(brew --prefix 2>/dev/null || true)"
  SEARCH_PREFIXES=()
  if [[ -n "$BREW_PREFIX" ]]; then
    SEARCH_PREFIXES+=("$BREW_PREFIX")
  fi
  # 兜底，加入常见前缀
  SEARCH_PREFIXES+=("/opt/homebrew" "/usr/local")

  echo "[macos-app] Using search prefixes for dylibs: ${SEARCH_PREFIXES[*]}"

  # 为主程序添加 RPATH 指向 Frameworks
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN" 2>/dev/null || true

  COPIED_LIST="$APP_FW/.copied_libs"
  rm -f "$COPIED_LIST"
  touch "$COPIED_LIST"

  copy_dep() {
    local src="$1"
    if [[ -z "$src" || ! -f "$src" ]]; then
      return
    fi

    local name
    name="$(basename "$src")"

    if grep -qx "$name" "$COPIED_LIST" 2>/dev/null; then
      return
    fi
    echo "$name" >> "$COPIED_LIST"

    local dest="$APP_FW/$name"
    echo "[macos-app] bundling dylib: $src -> $dest"
    cp -p "$src" "$dest" || return

    # 设置自身 ID
    install_name_tool -id "@rpath/$name" "$dest" 2>/dev/null || true

    # 递归处理此 dylib 的依赖
    local deps dep prefix match depname
    deps="$(otool -L "$dest" | tail -n +2 | awk '{print $1}')"
    for dep in $deps; do
      [[ -z "$dep" ]] && continue
      if [[ "$dep" == /usr/lib/* || "$dep" == /System/* ]]; then
        continue
      fi

      match=0
      for prefix in "${SEARCH_PREFIXES[@]}"; do
        [[ -z "$prefix" ]] && continue
        if [[ "$dep" == "$prefix"* ]]; then
          match=1
          break
        fi
      done
      if [[ "$match" -eq 0 ]]; then
        continue
      fi

      copy_dep "$dep"
      depname="$(basename "$dep")"
      install_name_tool -change "$dep" "@rpath/$depname" "$dest" 2>/dev/null || true
    done
  }

  # 处理主二进制的依赖
  MAIN_DEPS="$(otool -L "$BIN" | tail -n +2 | awk '{print $1}')"
  for dep in $MAIN_DEPS; do
    [[ -z "$dep" ]] && continue
    if [[ "$dep" == /usr/lib/* || "$dep" == /System/* ]]; then
      continue
    fi

    match=0
    for prefix in "${SEARCH_PREFIXES[@]}"; do
      [[ -z "$prefix" ]] && continue
      if [[ "$dep" == "$prefix"* ]]; then
        match=1
        break
      fi
    done
    if [[ "$match" -eq 0 ]]; then
      continue
    fi

    copy_dep "$dep"
    depname="$(basename "$dep")"
    install_name_tool -change "$dep" "@rpath/$depname" "$BIN" 2>/dev/null || true
  done
fi

# 6. Prepare DMG root folder (app + README / LICENSE) and create DMG
DMG_ROOT="$DIST_DIR/macos"
mkdir -p "$DMG_ROOT"

# 放入 .app
cp -R "$APP_ROOT" "$DMG_ROOT/"

# 放入 README / LICENSE，方便用户查看
if compgen -G "README*.md" >/dev/null 2>&1; then
  cp README*.md "$DMG_ROOT/" 2>/dev/null || true
fi
if [[ -f "LICENSE.txt" ]]; then
  cp LICENSE.txt "$DMG_ROOT/" 2>/dev/null || true
fi

DMG_PATH="$DIST_DIR/sfd_tool_macos.dmg"
rm -f "$DMG_PATH"

hdiutil create \
  -volname "SFD Tool" \
  -srcfolder "$DMG_ROOT" \
  -ov -format UDZO "$DMG_PATH"

cat <<EOF
[macos-app] Done.

生成的自包含应用位于：
  $APP_ROOT

已自动创建 DMG：
  $DMG_PATH

你可以手动测试：
  1) 在 Finder 中打开 $DIST_DIR
  2) 将 "SFD Tool.app" 拖到 /Applications
  3) 从 Launchpad 或 Finder 直接启动，无需额外 brew install gtk+3 libusb gettext
EOF
