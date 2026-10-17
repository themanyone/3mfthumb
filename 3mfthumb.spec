Name:           3mfthumb
Version:        1.2.2
Release:        1%{?dist}
Summary:        Embedded 3MF thumbnail extractor for desktop file managers

License:        GPL-2.0-or-later
URL:            https://github.com/themanyone/3mfthumb
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconf-pkg-config
BuildRequires:  lib3mf-devel
BuildRequires:  cairo-devel
BuildRequires:  libzip-devel
BuildRequires:  zlib-ng-compat-devel

%description
3mfthumb extracts the thumbnail image that 3D printing applications embed
in a 3MF package and writes it out as a PNG. It is a freedesktop.org
thumbnail generator, which desktop file managers such as PCManFM and
Nautilus call to show previews of .3mf files. Packages without an embedded
image get a simple rendered preview of their meshes instead.

%prep
%autosetup -n %{name}-%{version}

%build
# The untracked CMake harness only handles the Windows cross-build, so
# upstream's Makefile is used instead of %%cmake (and thus no Ninja).
# Include paths are passed explicitly (cairo's come from pkg-config) so the
# build does not depend on the packager's environment.
%make_build CXX="%{__cxx}" \
    CFLAGS="%{optflags} -std=c++17 -fPIE" \
    BUILD_CFLAGS="-I%{_includedir}/lib3mf $(pkg-config --cflags cairo)" \
    LDFLAGS="-l3mf -lzip -lz -lcairo -pie"

%install
# Upstream's install target stages directly from `make install`; these files
# go to /usr/bin and /usr/share/thumbnailers, which is where GNOME's thumbnail
# sandbox (it mounts only /usr) and Nautilus look for them.
install -Dpm 755 3mfthumb %{buildroot}%{_bindir}/3mfthumb
install -Dpm 644 3mf.thumbnailer %{buildroot}%{_datadir}/thumbnailers/3mf.thumbnailer
install -Dpm 644 LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
cp -pr win-installer win-shellext %{buildroot}%{_docdir}/%{name}/

# Ship the sources so rebuilding as documented works from the installed docs.
mkdir -p %{buildroot}%{_docdir}/%{name}/source
install -pm 644 Makefile 3mfthumb.cpp 3mf.thumbnailer LICENSE \
  %{buildroot}%{_docdir}/%{name}/source/

%files
%license %{_docdir}/%{name}/LICENSE
%doc %{_docdir}/%{name}/source
%doc %{_docdir}/%{name}/win-installer
%doc %{_docdir}/%{name}/win-shellext
%{_bindir}/3mfthumb
%{_datadir}/thumbnailers/3mf.thumbnailer

%changelog
* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.2.2-1
- Install to /usr/bin, the only place Nautilus' bubblewrap sandbox can
  reach; drop the GResource overlay that made Nautilus hang at startup
- Fix file:// URI decoding, which produced no thumbnail at all

* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.2.1-1
- Windows shell extension renders packages without an embedded thumbnail

* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.2.0-1
- Render the meshes when a package has no embedded preview image
- Add -r/--render to force rendering, and material colours from 3MF

* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.1.1-1
- Accept file:// URIs so GNOME/Nautilus sandboxed thumbnailers work

* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.1.0-1
- Initial package for the v1.1.0 release
