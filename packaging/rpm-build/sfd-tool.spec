# 禁用 debug 包
%global debug_package %{nil}
%global _enable_debug_package 0

# 禁用 LTO，避免 ld 在 .debug_* 段对 nlohmann/json 和内部静态符号报 undefined
%global _lto_cflags %{nil}

Name:           sfd-tool
Version:        1.8.3.4
Release:        1%{?dist}
Summary:        Spreadtrum Firmware Dumper Tool

License:        GPLv3+
URL:            https://github.com/C-Hidery/sfd_tool
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gtk3-devel
BuildRequires:  libusb1-devel
BuildRequires:  pkgconf-pkg-config
BuildRequires:  desktop-file-utils
BuildRequires:  gtk-update-icon-cache
Requires:       gtk3
Requires:       libusb1

%description
GUI tool for dumping and programming Spreadtrum device firmware.

%prep
%autosetup

%build
# 使用 CMake + Ninja 构建，始终启用 GTK 与 libusb
cmake -S . -B build_cmake -G "Ninja" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=%{_prefix} \
  -DUSE_GTK=ON \
  -DUSE_LIBUSB=ON
cmake --build build_cmake

%install
# 使用 CMake 的安装规则安装到 RPM 构建根目录
DESTDIR="%{buildroot}" cmake --install build_cmake --prefix "%{_prefix}"

%post
%{_bindir}/gtk-update-icon-cache -q -t -f %{_datadir}/icons/hicolor || :

%postun
%{_bindir}/gtk-update-icon-cache -q -t -f %{_datadir}/icons/hicolor || :

%files
%license %{_datadir}/doc/%{name}/LICENSE.txt
%{_bindir}/sfd_tool
%{_datadir}/applications/*.desktop
%{_datadir}/icons/hicolor/*/apps/sfd-tool.png
%{_mandir}/man1/sfd-tool.1*
%{_datadir}/locale/zh_CN/LC_MESSAGES/sfd_tool.mo
%doc %{_datadir}/doc/%{name}/*

%changelog
* Wed Apr 01 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.3.4-1-ltv
- 完善：文件夹分区恢复并新增备份完整性检查；优化：修改分区导致线程阻塞

* Thu Mar 26 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.3.3-1-ltv
- 修复：super 分区备份状态栏显示问题；优化：分区操作页按钮布局；新增：分区操作页新增“抹除所有分区”功能

* Wed Mar 25 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.3.2-1-ltv
- 优化批量恢复刷机体验；修复连接时间误触

* Wed Mar 25 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.3.1-1-ltv
- 优化：统一分区读取与全盘备份的数据块配置与显示

* Wed Mar 25 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.3.0-1-ltv
- 重构：UI相关的目录结构

* Tue Mar 24 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.2.5-1-ltv
- -inf

* Tue Mar 24 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.2.4-1-ltv
- -inf

* Mon Mar 23 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.2.3-1-ltv
- -inf

* Sat Mar 21 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.2.2-1-ltv
- -inf

* Sat Mar 21 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.2.1-1-ltv
- -inf

* Sat Mar 21 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.2.0-1-ltv
- -inf

* Fri Mar 20 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.1.0-1-ltv
- -inf

* Wed Mar 18 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.0.2-1-ltv
- -inf

* Tue Mar 17 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.0.1-1-ltv
- -inf

* Tue Mar 17 2026 RyanCrepa <Ryan110413@outlook.com> - 1.8.0.0-1-ltv
- -inf

* Mon Mar 16 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.16-1-ltv
- -inf

* Mon Mar 16 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.15-1-ltv
- -inf

* Mon Mar 16 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.14-1-ltv
- -inf

* Mon Mar 16 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.13-1-ltv
- -inf

* Mon Mar 16 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.12-1-ltv
- -inf

* Mon Mar 16 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.11-1-ltv
- -inf

* Mon Mar 16 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.10-1-ltv
- -inf

* Sun Mar 15 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.9-1-ltv
- -inf

* Sun Mar 15 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.8-1-ltv
- -inf

* Sun Mar 15 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.7-1-ltv
- -inf

* Sun Mar 15 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.6-1-ltv
- -inf

* Sun Mar 15 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.5-1-ltv
- -inf

* Fri Feb 20 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.7.4-1-ltv
- Bump upstream to 1.7.7.4 LTV
- UI refactor and logic optimizations
- Fix CI build issues for x86 (GitHub Actions)
- Fix missing .mo file when packaging
- Fix build.yml parsing error
- Add version update helper scripts and docs/VERSION_LOG.md integration

* Fri Feb 20 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.6.0-1-ltv
- Initial RPM package (migrated to CMake-based build)
