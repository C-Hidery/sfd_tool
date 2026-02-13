#!/bin/bash
# sfd_tool DEB build script
set -e

VERSION="1.7.3.4"
APPNAME="sfd_tool"
PKGNAME="sfd-tool"
ARCHIVE="${PKGNAME}-${VERSION}"

# 设置环境变量
export DEBFULLNAME="RyanCrepa"
export DEBEMAIL="Ryan110413@outlook.com"

echo "=== Building $PKGNAME version $VERSION ==="

# 清理旧文件
rm -rf dist debian/$PKGNAME
make clean 2>/dev/null || true

# 构建应用
make release

# 创建临时构建目录
rm -rf /tmp/build-$PKGNAME
mkdir -p /tmp/build-$PKGNAME/$ARCHIVE

# 复制文件
cp -r *.cpp *.h *.hpp *.txt *.md *.desktop Makefile Lib /tmp/build-$PKGNAME/$ARCHIVE/
[ -f icon.png ] && cp icon.png /tmp/build-$PKGNAME/$ARCHIVE/

# 复制debian目录
if [ -d debian ]; then
    cp -r debian /tmp/build-$PKGNAME/$ARCHIVE/
else
    echo "Error: No debian directory found"
    exit 1
fi

# 进入构建目录
cd /tmp/build-$PKGNAME/$ARCHIVE

# 构建包
dpkg-buildpackage -us -uc

# 复制生成的包回项目目录
cp ../*.deb ~/Source/sfd_tool/ 2>/dev/null || true

echo "=== Build complete ==="
echo "Packages:"
ls -la ../*.deb 2>/dev/null || echo "Packages in /tmp/build-$PKGNAME/"
