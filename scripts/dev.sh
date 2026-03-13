#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build_cmake_debug"

# 简单开发构建脚本：每次重新配置并编译 Debug 版本

if ! command -v cmake >/dev/null 2>&1; then
  echo "[dev] 错误：未找到 cmake 命令"
  exit 1
fi

GENERATOR="Ninja"
if ! command -v ninja >/dev/null 2>&1; then
  GENERATOR="Unix Makefiles"
fi

mkdir -p "${BUILD_DIR}"

cmake -S . -B "${BUILD_DIR}" -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "${BUILD_DIR}" -j

echo "[dev] 启动 sfd_tool (Debug) ..."
LC_ALL=zh_CN.UTF-8 "./${BUILD_DIR}/sfd_tool"
