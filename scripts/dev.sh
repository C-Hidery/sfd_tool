#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build_cmake_debug"
I18N_SOURCES="main.cpp ui/GtkWidgetHelper.cpp ui/ui_common.cpp pages/*.cpp"

# 简单开发构建脚本：每次重新配置并编译 Debug 版本

update_i18n() {
  if ! command -v xgettext >/dev/null 2>&1; then
    echo "[dev][i18n] 未找到 xgettext，跳过 POT/PO 更新" >&2
    return
  fi

  echo "[dev][i18n] 更新 locale/sfd_tool.pot ..."
  xgettext \
    --language=C++ \
    --keyword=_ \
    --from-code=UTF-8 \
    --output=locale/sfd_tool.pot \
    main.cpp ui/GtkWidgetHelper.cpp pages/page_*.cpp ui/ui_common.cpp

  local PYTHON_BIN="${PYTHON:-python3}"
  if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    PYTHON_BIN="python"
  fi

  if command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    echo "[dev][i18n] 同步 zh_CN sfd_tool.po ..."
    "${PYTHON_BIN}" scripts/gen_po.py || \
      echo "[dev][i18n] gen_po.py 执行失败，跳过本次翻译同步" >&2
  else
    echo "[dev][i18n] 未找到 python3/python，跳过 gen_po.py" >&2
  fi
}

if ! command -v cmake >/dev/null 2>&1; then
  echo "[dev] 错误：未找到 cmake 命令"
  exit 1
fi

# 在配置/构建前更新 POT/PO，避免漏翻
update_i18n

GENERATOR="Ninja"
if ! command -v ninja >/dev/null 2>&1; then
  GENERATOR="Unix Makefiles"
fi

mkdir -p "${BUILD_DIR}"

cmake -S . -B "${BUILD_DIR}" -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSFD_ENABLE_TESTS=ON

cmake --build "${BUILD_DIR}" -j

ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "[dev] 启动 sfd_tool (Debug) ..."
"./${BUILD_DIR}/sfd_tool"
