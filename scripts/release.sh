#!/usr/bin/env bash
set -euo pipefail

# 在项目根目录下运行：
#   ./scripts/release.sh
#
# 功能：
# - 使用 CMake 生成/更新 Release 构建目录（build_cmake）
# - 优先使用 Ninja；若未安装 ninja，则退回 Unix Makefiles
# - 编译 Release 版本（不自动运行，可手动执行二进制）

BUILD_DIR="build_cmake"
I18N_SOURCES="main.cpp GtkWidgetHelper.cpp pages/page_*.cpp ui_common.cpp"

OS_NAME="$(uname -s 2>/dev/null || echo unknown)"

update_i18n() {
  if ! command -v xgettext >/dev/null 2>&1; then
    echo "[release][i18n] 未找到 xgettext，跳过 POT/PO 更新" >&2
    return
  fi

  echo "[release][i18n] 更新 locale/sfd_tool.pot ..."
  xgettext \
    --language=C++ \
    --keyword=_ \
    --from-code=UTF-8 \
    --output=locale/sfd_tool.pot \
    ${I18N_SOURCES}

  local PYTHON_BIN="${PYTHON:-python3}"
  if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    PYTHON_BIN="python"
  fi

  if command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    echo "[release][i18n] 同步 zh_CN sfd_tool.po ..."
    "${PYTHON_BIN}" scripts/gen_po.py || \
      echo "[release][i18n] gen_po.py 执行失败，跳过本次翻译同步" >&2
  else
    echo "[release][i18n] 未找到 python3/python，跳过 gen_po.py" >&2
  fi
}

# 1. 检查 cmake 是否存在
if ! command -v cmake >/dev/null 2>&1; then
  echo "[release] 错误：未找到 cmake 命令"
  case "${OS_NAME}" in
    Darwin)
      echo "  macOS 安装示例：brew install cmake"
      ;;
    Linux*)
      echo "  Debian/Ubuntu：sudo apt-get update && sudo apt-get install cmake"
      echo "  Fedora：sudo dnf install cmake"
      echo "  Arch：sudo pacman -S cmake"
      ;;
    *)
      echo "  请参考官方文档安装 CMake：https://cmake.org/download/"
      ;;
  esac
  exit 1
fi

# 在配置/构建前更新 POT/PO，避免漏翻
update_i18n

# 2. 选择生成器（优先 Ninja，没有则回退 Unix Makefiles）
GENERATOR="Ninja"
if ! command -v ninja >/dev/null 2>&1; then
  GENERATOR="Unix Makefiles"
  echo "[release] 提示：未检测到 ninja，将使用 Unix Makefiles。"
  case "${OS_NAME}" in
    Darwin)
      echo "  建议安装 ninja：brew install ninja"
      ;;
    Linux*)
      echo "  建议安装 ninja（示例：sudo apt-get install ninja-build）"
      ;;
  esac
fi

# 3. 如使用 Unix Makefiles，则简单检查 make 是否存在
if [ "${GENERATOR}" = "Unix Makefiles" ] && ! command -v make >/dev/null 2>&1; then
  echo "[release] 错误：生成器为 Unix Makefiles，但系统未找到 make。"
  case "${OS_NAME}" in
    Darwin)
      echo "  请先安装 Xcode Command Line Tools：xcode-select --install"
      ;;
    Linux*)
      echo "  Debian/Ubuntu：sudo apt-get install build-essential"
      ;;
  esac
  exit 1
fi

echo "[release] 使用生成器: ${GENERATOR}"

# 为避免 Xcode/环境变量强行提升最低版本，这里显式设置
export MACOSX_DEPLOYMENT_TARGET="11.0"

echo "[release] 配置 Release 构建..."
cmake -S . -B "${BUILD_DIR}" -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

echo "[release] 编译 Release 构建..."
cmake --build "${BUILD_DIR}" -j

echo

echo "[release] 构建完成：${BUILD_DIR}/sfd_tool"
echo "你可以这样运行 Release 版（界面语言由 per-user 配置文件中的 sfd_tool_config.json 控制）："
echo "./${BUILD_DIR}/sfd_tool"
