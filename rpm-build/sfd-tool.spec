Name:           sfd-tool
Version:        1.7.2.3
Release:        1%{?dist}
Summary:        Spreadtrum Firmware Dumper Tool

License:        GPLv3+
URL:            https://github.com/C-Hidery/sfd_tool
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
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
make GTK=1 LIBUSB=1 release

%install
# 创建必要的目录
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/16x16/apps
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/32x32/apps
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/48x48/apps
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/256x256/apps
mkdir -p %{buildroot}%{_datadir}/doc/%{name}

# 安装二进制文件
install -m 755 sfd_tool %{buildroot}%{_bindir}/

cp sfd_tool.desktop %{buildroot}%{_datadir}/applications/

# 安装图标（如果有）
if [ -f icon.png ]; then
    convert icon.png -resize 16x16 %{buildroot}%{_datadir}/icons/hicolor/16x16/apps/sfd-tool.png
    convert icon.png -resize 32x32 %{buildroot}%{_datadir}/icons/hicolor/32x32/apps/sfd-tool.png
    convert icon.png -resize 48x48 %{buildroot}%{_datadir}/icons/hicolor/48x48/apps/sfd-tool.png
    convert icon.png -resize 256x256 %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/sfd-tool.png
fi

# 安装文档
install -m 644 LICENSE.txt %{buildroot}%{_datadir}/doc/%{name}/
[ -f README.md ] && install -m 644 README.md %{buildroot}%{_datadir}/doc/%{name}/
[ -f README_ZH.md ] && install -m 644 README_ZH.md %{buildroot}%{_datadir}/doc/%{name}/

%post
%{_bindir}/gtk-update-icon-cache -q -t -f %{_datadir}/icons/hicolor || :

%postun
%{_bindir}/gtk-update-icon-cache -q -t -f %{_datadir}/icons/hicolor || :

%files
%license LICENSE.txt
%{_bindir}/sfd_tool
%{_datadir}/applications/sfd_tool.desktop
%{_datadir}/icons/hicolor/*/apps/sfd-tool.png
%doc %{_datadir}/doc/%{name}/*

%changelog
* Tue Feb 10 2026 RyanCrepa <Ryan110413@outlook.com> - 1.7.2.3-1
- Initial RPM package