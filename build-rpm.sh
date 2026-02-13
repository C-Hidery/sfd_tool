#!/bin/bash
# sfd_tool RPM build script
set -e

VERSION="1.7.3.4"
APPNAME="sfd_tool"
PKGNAME="sfd-tool"

echo "=== Building $PKGNAME RPM version $VERSION ==="

# 清理
make clean 2>/dev/null || true
rm -rf ~/rpmbuild

# 设置 RPM 构建环境
mkdir -p ~/rpmbuild/{SOURCES,SPECS,RPMS,SRPMS,BUILD}

# 创建源码包
mkdir -p /tmp/$PKGNAME-$VERSION
cp -r *.cpp *.h *.hpp *.txt *.md *.desktop *.man Makefile Lib /tmp/$PKGNAME-$VERSION/
[ -f icon.png ] && cp icon.png /tmp/$PKGNAME-$VERSION/

# 复制 spec 文件
if [ -f rpm-build/$PKGNAME.spec ]; then
    cp rpm-build/$PKGNAME.spec ~/rpmbuild/SPECS/
else
    echo "Error: $PKGNAME.spec not found"
    exit 1
fi

# 创建 tarball
cd /tmp
tar -czf ~/rpmbuild/SOURCES/$PKGNAME-$VERSION.tar.gz $PKGNAME-$VERSION
cd -

# 构建 RPM
rpmbuild -ba ~/rpmbuild/SPECS/$PKGNAME.spec

echo "=== RPM Build complete ==="
echo "RPM packages created in:"
ls -la ~/rpmbuild/RPMS/*/*.rpm 2>/dev/null || echo "Check ~/rpmbuild/RPMS/"
ls -la ~/rpmbuild/SRPMS/*.rpm 2>/dev/null || echo "Check ~/rpmbuild/SRPMS/"
