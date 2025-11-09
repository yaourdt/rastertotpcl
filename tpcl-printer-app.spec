Name:           tpcl-printer-app
Version:        @@VERSION@@
Release:        1%{?dist}
Summary:        Printer driver for Toshiba TEC TPCL label printers
%global pappl_version 1.4.9

# Main application is GPL-3.0-or-later, but bundles PAPPL (Apache-2.0) statically
License:        GPL-3.0-or-later AND Apache-2.0
URL:            https://github.com/yaourdt/rastertotpcl
# Bundled libraries
Provides:       bundled(pappl) = %{pappl_version}
Source0:        %{name}-%{version}.tar.gz
Source1:        https://github.com/michaelrsweet/pappl/releases/download/v%{pappl_version}/pappl-%{pappl_version}.tar.gz#/pappl-%{pappl_version}.tar.gz
Source2:        tpcl-printer-app.service

BuildRequires:  gcc
BuildRequires:  systemd-rpm-macros
BuildRequires:  make
BuildRequires:  ImageMagick
BuildRequires:  pkgconfig
BuildRequires:  cups-devel
%if 0%{?suse_version}
BuildRequires:  libjpeg8-devel
BuildRequires:  libusb-1_0-devel
%else
BuildRequires:  libjpeg-devel
BuildRequires:  libusb1-devel
%endif
BuildRequires:  libpng-devel
BuildRequires:  zlib-devel
BuildRequires:  openssl-devel
BuildRequires:  pam-devel
BuildRequires:  avahi-devel

# Runtime dependencies are auto-detected from linked libraries
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd

%description
This is a modern printer application for Toshiba TEC label printers
that use the TPCL (TEC Printer Command Language) version 2 protocol.

It provides IPP-compatible printing with a convenient web-based
interface for configuring printer settings, replacing the older
PPD-based driver system.

%prep
%autosetup -n %{name}-%{version} -a 1

# Unpack PAPPL release tarball and place into source tree
rm -rf external/pappl
mkdir -p external
mv pappl-%{pappl_version} external/pappl

%build
export GIT_COMMIT=%{version}
# Build PAPPL first
cd external/pappl
./configure --prefix=%{_prefix}
cd ../..
./scripts/patch-translations.sh
cd external/pappl
%make_build
cd ../..
# Build tpcl-printer-app with package-build flag
%make_build package-build=1

%install
install -Dm0755 bin/tpcl-printer-app \
  %{buildroot}%{_bindir}/tpcl-printer-app
install -Dm0644 %{SOURCE2} \
  %{buildroot}%{_unitdir}/tpcl-printer-app.service

%post
%systemd_post tpcl-printer-app.service

%preun
%systemd_preun tpcl-printer-app.service

%postun
%systemd_postun_with_restart tpcl-printer-app.service

%files
%license LICENSE
%doc README.md
%{_bindir}/tpcl-printer-app
%{_unitdir}/tpcl-printer-app.service

%changelog
* Sun Nov 09 2025 Mark Dornbach <mark@dornbach.io>
- Inital RPM package
- For full changelog, see https://github.com/yaourdt/rastertotpcl
