Name:           tpcl-printer-app
Version:        0.2.3
Release:        1%{?dist}
Summary:        Printer driver for Toshiba TEC TPCL label printers

License:        GPL-3.0-or-later
URL:            https://github.com/yaourdt/rastertotpcl
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  git
BuildRequires:  ImageMagick
BuildRequires:  pkgconfig
BuildRequires:  cups-devel
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  libpng-devel
BuildRequires:  zlib-devel
BuildRequires:  openssl-devel
BuildRequires:  libusb1-devel
BuildRequires:  pam-devel
BuildRequires:  avahi-devel

Requires:       cups-libs
Requires:       libjpeg-turbo
Requires:       libpng16
Requires:       zlib
Requires:       openssl-libs
Requires:       libusb1
Requires:       pam
Requires:       avahi-libs

%description
This is a modern printer application for Toshiba TEC label printers
that use the TPCL (TEC Printer Command Language) version 2 protocol.

It provides IPP-compatible printing with a convenient web-based
interface for configuring printer settings, replacing the older
PPD-based driver system.

%prep
%setup -q

%build
make full

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT PREFIX=/usr

%files
/usr/bin/tpcl-printer-app

%changelog
* Sat Nov 09 2025 Mark Dornbach <mark@dornbach.io>
- Inital RPM package
- For full changelog, see https://github.com/yaourdt/rastertotpcl
