# 禁用 debug 包
%global debug_package %{nil}
%global _enable_debug_package 0


Name:           sfd-tool
Version:        1.7.6.0
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
  -DUSE_GTK=ON \
  -DUSE_LIBUSB=ON
cmake --build build_cmake -- -j

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
* Fri Feb 20 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.6.0-1-ltv
- Initial RPM package (migrated to CMake-based build)
