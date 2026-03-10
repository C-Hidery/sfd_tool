#!/usr/bin/env bash
set -euo pipefail

# 在项目根目录下运行：
#   ./scripts/dev.sh
#
# 功能：
# - 使用 CMake 生成/更新 Debug 构建目录（build_cmake_debug）
# - 优先使用 Ninja；若未安装 ninja，则退回 Unix Makefiles
# - 编译 Debug 版本
# - 以中文界面启动 sfd_tool（可通过预先设置 LC_ALL 覆盖）

BUILD_DIR="build_cmake_debug"

OS_NAME="$(uname -s 2>/dev/null || echo unknown)"

# 1. 检查 cmake 是否存在
if ! command -v cmake >/dev/null 2>&1; then
  echo "[dev] 错误：未找到 cmake 命令"
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

# 2. 选择生成器（优先 Ninja，没有则回退 Unix Makefiles）
GENERATOR="Ninja"
if ! command -v ninja >/dev/null 2>&1; then
  GENERATOR="Unix Makefiles"
  echo "[dev] 提示：未检测到 ninja，将使用 Unix Makefiles。"
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
  echo "[dev] 错误：生成器为 Unix Makefiles，但系统未找到 make。"
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

echo "[dev] 使用生成器: ${GENERATOR}"

echo "[dev] 配置 Debug 构建..."
cmake -S . -B "${BUILD_DIR}" -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "[dev] 编译 Debug 构建..."
cmake --build "${BUILD_DIR}" -j

echo "[dev] 启动 sfd_tool (Debug) ..."
LC_ALL="${LC_ALL:-zh_CN.UTF-8}" "./${BUILD_DIR}/sfd_tool"
