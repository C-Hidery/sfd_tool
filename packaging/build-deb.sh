#!/bin/bash
# sfd_tool DEB build script
set -e

VERSION="1.7.6.0"
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


# 创建临时构建目录
rm -rf /tmp/build-$PKGNAME
mkdir -p /tmp/build-$PKGNAME/$ARCHIVE

# 复制文件
cp -r *.cpp *.h *.hpp *.txt *.md Makefile third_party packaging scripts assets locale pages /tmp/build-$PKGNAME/$ARCHIVE/
[ -f assets/icon.png ] && cp assets/icon.png /tmp/build-$PKGNAME/$ARCHIVE/
cp packaging/sfd_tool.desktop /tmp/build-$PKGNAME/$ARCHIVE/

# 复制debian目录
if [ -d packaging/debian ]; then
    cp -r packaging/debian /tmp/build-$PKGNAME/$ARCHIVE/
    cp packaging/man_sfd-tool.1 /tmp/build-$PKGNAME/$ARCHIVE/
else
    echo "Error: No debian directory found"
    exit 1
fi

# 进入构建目录
cd /tmp/build-$PKGNAME/$ARCHIVE

# 构建包
dpkg-buildpackage -us -uc

# 复制生成的包回项目目录
cp ../*.deb ~/ 2>/dev/null || true

echo "=== Build complete ==="
echo "Packages:"
ls -la ../*.deb 2>/dev/null || echo "Packages in /tmp/build-$PKGNAME/"
