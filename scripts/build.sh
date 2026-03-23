#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build_cmake_debug"
I18N_SOURCES="main.cpp GtkWidgetHelper.cpp pages/page_*.cpp ui_common.cpp"

# 非交互构建脚本：更新翻译、配置并编译 Debug 版本，运行测试但不启动 GUI

update_i18n() {
  if ! command -v xgettext >/dev/null 2>&1; then
    echo "[build][i18n] 未找到 xgettext，跳过 POT/PO 更新" >&2
    return
  fi

  echo "[build][i18n] 更新 locale/sfd_tool.pot ..."
  xgettext \
    --language=C++ \
    --keyword=_ \
    --from-code=UTF-8 \
    --output=locale/sfd_tool.pot \
    main.cpp GtkWidgetHelper.cpp pages/page_*.cpp ui_common.cpp

  local PYTHON_BIN="${PYTHON:-python3}"
  if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    PYTHON_BIN="python"
  fi

  if command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    echo "[build][i18n] 同步 zh_CN sfd_tool.po ..."
    "${PYTHON_BIN}" scripts/gen_po.py || \
      echo "[build][i18n] gen_po.py 执行失败，跳过本次翻译同步" >&2
  else
    echo "[build][i18n] 未找到 python3/python，跳过 gen_po.py" >&2
  fi
}

if ! command -v cmake >/dev/null 2>&1; then
  echo "[build] 错误：未找到 cmake 命令"
  exit 1
fi

# 在配置/构建前更新 POT/PO，避免漏翻
update_i18n

GENERATOR="Ninja"
if ! command -v ninja >/dev/null 2>&1; then
  GENERATOR="Unix Makefiles"
fi

mkdir -p "${BUILD_DIR}"

echo "[build] 配置 Debug 构建..."
cmake -S . -B "${BUILD_DIR}" -G "${GENERATOR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSFD_ENABLE_TESTS=ON

echo "[build] 编译 Debug 构建..."
cmake --build "${BUILD_DIR}" -j

echo "[build] 运行测试..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "[build] 完成（未启动 GUI）。"
