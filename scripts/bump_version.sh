#!/usr/bin/env bash
# bump_version.sh - 交互式更新 sfd_tool 版本号和相关文件，并提交 git

set -euo pipefail

# 脚本所在目录的上一层就是仓库根目录
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# 确保暂存区干净，避免把其他改动一起一起提交
if ! git diff --cached --quiet; then
  echo "Error: 暂存区已有改动，请先提交或取消暂存 (git reset) 再运行本脚本。" >&2
  exit 1
fi

# 读取版本号（容错：自动去掉空白、中文句号等）
while :; do
  read -erp "请输入新版本号 (格式 X.Y.Z.W，例如 1.7.8.0，输入 q 退出): " NEW_VERSION_RAW
  # 允许直接退出
  if [[ "$NEW_VERSION_RAW" == "q" || "$NEW_VERSION_RAW" == "Q" ]]; then
    echo "已取消"
    exit 0
  fi

  # 清理输入：去掉空白，将中文句号替换为英文句号，只保留数字和点
  CLEANED="$(printf '%s' "$NEW_VERSION_RAW" | tr -d '[:space:]')"
  CLEANED="${CLEANED//。/.}"
  CLEANED="$(printf '%s' "$CLEANED" | LC_ALL=C tr -cd '0-9.')"

  NEW_VERSION="$CLEANED"

  # 简单校验版本号格式：X.Y.Z.W
  if printf '%s\n' "$NEW_VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$'; then
    break
  fi
  echo "Error: 版本号格式必须类似 1.7.8.0，请重新输入。"
done

echo
read -erp "请输入本次更新内容（用于 docs/VERSION_LOG.md，单行描述即可）: " LOG_LINE
if [[ -z "$LOG_LINE" ]]; then
  LOG_LINE="No details provided."
fi
LOG_LINES=("$LOG_LINE")

# 统一的 sed -i 封装（兼容 GNU sed 和 BSD/macOS sed）
sed_inplace_e() {
  local script="$1"
  local file="$2"
  if sed --version >/dev/null 2>&1; then
    # GNU sed
    sed -i -E "$script" "$file"
  else
    # BSD/macOS sed
    sed -i '' -E "$script" "$file"
  fi
}

echo "[1/4] 更新 VERSION.txt..."
printf '%s\n' "$NEW_VERSION" > VERSION.txt

# docs/VERSION_LOG.md 头部展示版本 + 追加日志
if [[ -f docs/VERSION_LOG.md ]]; then
  echo "[2/4] 更新 docs/VERSION_LOG.md 头部版本..."
  # 只替换第一处匹配的 "Version ... LTV Edition"，保留后面的历史日志
  tmp_file="$(mktemp)"
  awk -v ver="$NEW_VERSION" '
    BEGIN { replaced=0 }
    /^Version .* LTV Edition$/ && !replaced {
      print "Version " ver " LTV Edition"
      replaced=1
      next
    }
    { print }
  ' docs/VERSION_LOG.md > "$tmp_file"
  mv "$tmp_file" docs/VERSION_LOG.md

  echo "[3/4] 追加本次版本日志到 docs/VERSION_LOG.md..."

  # 把新版本日志插入到上一条日志的正下方（紧贴上一版本），同时保持尾注在末尾
  if grep -q '^---v [0-9]\+\.[0-9]\+\.[0-9]\+\.[0-9]\+---$' docs/VERSION_LOG.md; then
    head_tmp="$(mktemp)"
    footer_tmp="$(mktemp)"

    # 先按尾注拆分：上半部分(包含所有日志) + 尾注
    awk -v head="$head_tmp" -v foot="$footer_tmp" '
      BEGIN { footer=0 }
      /^Under GPL v3 License/ { footer=1 }
      footer==0 { print > head; next }
      { print > foot }
    ' docs/VERSION_LOG.md

    # 在 head 里追加新日志块（直接追加在最后一个 ---v ...--- 之后，不额外插空行）
    {
      cat "$head_tmp"
      echo "---v $NEW_VERSION---"
      for line in "${LOG_LINES[@]}"; do
        echo "$line"
      done
    } > docs/VERSION_LOG.md

    # 再把尾注接回去，前面保留一个空行
    {
      echo ""  >> docs/VERSION_LOG.md
      cat "$footer_tmp" >> docs/VERSION_LOG.md
    }

    rm -f "$head_tmp" "$footer_tmp"
  else
    # 没有找到尾注标记，就直接紧贴末尾追加
    {
      echo "---v $NEW_VERSION---"
      for line in "${LOG_LINES[@]}"; do
        echo "$line"
      done
    } >> docs/VERSION_LOG.md
  fi
else
  echo "Warning: docs/VERSION_LOG.md 不存在，跳过" >&2
fi

# RPM spec 里的 Version 字段
if [[ -f packaging/rpm-build/sfd-tool.spec ]]; then
  echo "[4/4] 更新 packaging/rpm-build/sfd-tool.spec 版本号..."
  sed_inplace_e "s/^Version:[[:space:]]*[0-9.]+/Version:        $NEW_VERSION/" \
    packaging/rpm-build/sfd-tool.spec
else
  echo "Warning: packaging/rpm-build/sfd-tool.spec 不存在，跳过" >&2
fi

# Debian changelog 条目（可选）
if [[ -f packaging/debian/changelog ]]; then
  echo "[附加] 更新 packaging/debian/changelog..."
  DEB_VERSION="${NEW_VERSION}-1-ltv"
  DEB_DATE="$(date -R)"
  DEB_FILE="packaging/debian/changelog"
  tmp_file="$(mktemp)"
  {
    echo "sfd-tool ($DEB_VERSION) unstable; urgency=medium"
    echo
    echo "  * $LOG_LINE"
    echo
    echo " -- RyanCrepa <Ryan110413@outlook.com>  $DEB_DATE"
    echo
    cat "$DEB_FILE"
  } > "$tmp_file"
  mv "$tmp_file" "$DEB_FILE"
else
  echo "Warning: packaging/debian/changelog 不存在，跳过" >&2
fi

# RPM spec 的 %changelog 条目（可选）
if [[ -f packaging/rpm-build/sfd-tool.spec ]]; then
  echo "[附加] 更新 packaging/rpm-build/sfd-tool.spec %changelog..."
  RPM_FILE="packaging/rpm-build/sfd-tool.spec"
  RPM_DATE="$(LC_ALL=C date '+%a %b %d %Y')"
  RPM_VER="${NEW_VERSION}-1-ltv"
  tmp_file="$(mktemp)"
  awk -v d="$RPM_DATE" -v v="$RPM_VER" -v log="$LOG_LINE" '
    BEGIN { inserted=0 }
    /^%changelog/ && !inserted {
      print "%changelog"
      print "* " d " RyanCrepa <Ryan110413@outlook.com> - " v
      print "- " log
      print ""
      inserted=1
      next
    }
    { print }
  ' "$RPM_FILE" > "$tmp_file"
  mv "$tmp_file" "$RPM_FILE"
fi

# Windows 资源文件中的版本（可选）
if [[ -f app.rc ]]; then
  echo "[附加] 更新 app.rc 里的版本号..."
  VERSION_COMMA="${NEW_VERSION//./,}"

  # 数字版本: FILEVERSION / PRODUCTVERSION
  sed_inplace_e "s/^( FILEVERSION )[0-9,]+/\\1$VERSION_COMMA/" app.rc
  sed_inplace_e "s/^( PRODUCTVERSION )[0-9,]+/\\1$VERSION_COMMA/" app.rc

  # 字符串版本: \"FileVersion\" / \"ProductVersion\"
  sed_inplace_e "s/(\"FileVersion\", \"?)[0-9.]+(\"?)/\\1$NEW_VERSION\\2/" app.rc
  sed_inplace_e "s/(\"ProductVersion\", \"?)[0-9.]+(\"?)/\\1$NEW_VERSION\\2/" app.rc
else
  echo "Warning: app.rc 不存在，跳过" >&2
fi

# 提交到 git

echo
echo "Git 变更预览："
git status --short VERSION.txt docs/VERSION_LOG.md packaging/rpm-build/sfd-tool.spec app.rc || true

echo
echo "正在提交版本变更..."
git add VERSION.txt
[[ -f docs/VERSION_LOG.md ]] && git add docs/VERSION_LOG.md
[[ -f packaging/rpm-build/sfd-tool.spec ]] && git add packaging/rpm-build/sfd-tool.spec
[[ -f app.rc ]] && git add app.rc
[[ -f packaging/debian/changelog ]] && git add packaging/debian/changelog

git commit -m "Version: $NEW_VERSION"

echo "完成：已更新版本号为 $NEW_VERSION 并提交到 git。"
echo "记得重新跑 CMake 配置和编译以生成新的 version.h。"
