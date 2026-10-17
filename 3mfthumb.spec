Name:           3mfthumb
Version:        1.1.0
Release:        1%{?dist}
Summary:        Embedded 3MF thumbnail extractor for desktop file managers

License:        GPL-2.0-or-later
URL:            https://github.com/themanyone/3mfthumb
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconf-pkg-config
BuildRequires:  lib3mf-devel
BuildRequires:  libzip-devel
BuildRequires:  zlib-ng-compat-devel

%description
3mfthumb extracts the thumbnail image that 3D printing applications embed
in a 3MF package and writes it out as a PNG. It is a freedesktop.org
thumbnail generator, which desktop file managers such as PCManFM and
Nautilus call to show previews of .3mf files. The 3D model itself is
never rendered.

%prep
%autosetup -n %{name}-%{version}

%build
# The untracked CMake harness only handles the Windows cross-build, so
# upstream's Makefile is used instead of %%cmake (and thus no Ninja).
# Build-time pkg-config is replaced by explicit flags so the build does not
# depend on the packager's environment.
%make_build CXX="%{__cxx}" \
    CFLAGS="%{optflags} -std=c++17 -fPIE" \
    BUILD_CFLAGS="-I%{_includedir}/lib3mf" \
    LDFLAGS="-l3mf -lzip -lz -pie"

%install
# Upstream's install target assumes a user-local prefix (DESTDIR/bin), so
# stage the files directly rather than through `make install`.
install -Dpm 755 3mfthumb %{buildroot}%{_bindir}/3mfthumb
install -Dpm 644 3mf.thumbnailer %{buildroot}%{_datadir}/thumbnailers/3mf.thumbnailer
install -Dpm 644 LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
cp -pr win-installer win-shellext %{buildroot}%{_docdir}/%{name}/

# Ship the Makefile configured for the layout this RPM installs, so
# rebuilding as documented does not target a home directory from a package
# build. The other sources are already present in the build tree.
mkdir -p %{buildroot}%{_docdir}/%{name}/source
install -pm 644 Makefile %{buildroot}%{_docdir}/%{name}/source/
sed -i 's|^INSTALL_HOME=$(HOME)/\.local$|INSTALL_HOME=$(PREFIX)|' \
    %{buildroot}%{_docdir}/%{name}/source/Makefile

%files
%license %{_docdir}/%{name}/LICENSE
%doc %{_docdir}/%{name}/source
%doc %{_docdir}/%{name}/win-installer
%doc %{_docdir}/%{name}/win-shellext
%{_bindir}/3mfthumb
%{_datadir}/thumbnailers/3mf.thumbnailer

%changelog
* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.1.0-1
- Initial package for the v1.1.0 release
