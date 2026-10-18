Name:           3mfthumb
Version:        1.2.3
Release:        1%{?dist}
Summary:        Embedded 3MF thumbnail extractor for desktop file managers
# The KDE plugin lives in its own subpackage so the Qt 6 and KIO dependencies
# stay out of the base package. Build without it: --without kde
%bcond_without kde

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

%if %{with kde}
BuildRequires:  cmake
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kio-devel
%endif

%description
3mfthumb extracts the thumbnail image that 3D printing applications embed
in a 3MF package and writes it out as a PNG. It is a freedesktop.org
thumbnail generator, which desktop file managers such as PCManFM and
Nautilus call to show previews of .3mf files. Packages without an embedded
image get a simple rendered preview of their meshes instead.

%package kde
Summary:        KDE Dolphin thumbnail plugin for 3MF and STL files
License:        GPL-2.0-or-later
Requires:       %{name} = %{version}-%{release}

%description kde
KIO thumbnail plugin that shows previews of 3MF and STL files in KDE
applications such as Dolphin and the file dialog. KDE does not read
freedesktop .thumbnailer entries, so the %{name} binary alone is not
enough there. This is a script that runs the %{name} binary with -c and
lets KIO cache the result, which keeps the package free of Qt and KIO
dependencies and safe to install on non-KDE systems.

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
cp -pr win-installer win-shellext kde-shellext %{buildroot}%{_docdir}/%{name}/

%if %{with kde}
# KDE thumbnail plugin. It carries the Qt/KIO dependency, so it lives in the
# -kde subpackage, and the freedesktop thumbnailer above stays the only thing
# the base package installs. Plain cmake calls: the %%cmake macros force their
# own build directory, which would collide with build-kde.
cmake -S kde-shellext -B build-kde -DCMAKE_INSTALL_PREFIX=%{_prefix}
cmake --build build-kde --verbose %{?_smp_mflags}
DESTDIR=%{buildroot} cmake --install build-kde
%endif

# Ship the sources so rebuilding as documented works from the installed docs.
mkdir -p %{buildroot}%{_docdir}/%{name}/source
install -pm 644 Makefile 3mfthumb.cpp 3mf.thumbnailer LICENSE \
  %{buildroot}%{_docdir}/%{name}/source/

%files
%license %{_docdir}/%{name}/LICENSE
%doc %{_docdir}/%{name}/source
%doc %{_docdir}/%{name}/win-installer
%doc %{_docdir}/%{name}/win-shellext
%doc %{_docdir}/%{name}/kde-shellext
%{_bindir}/3mfthumb
%{_datadir}/thumbnailers/3mf.thumbnailer

%if %{with kde}
%files kde
%doc %{_docdir}/%{name}/kde-shellext
%{_libdir}/qt6/plugins/thumbcreator/lib3mfthumbnail.so
%{_libdir}/qt6/plugins/thumbcreator/3mfthumbnail.json
%endif

%changelog
* Sun Oct 18 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.2.3-1
- Show .glb (binary glTF) previews, using an embedded preview when the
  asset has one and rendering the scene otherwise
- Render STL models and add the -c (thumbnail cache), -s (size), -f/-b
  (colours) and -x/-y/-z (camera aim) options
- Add the 3mfthumb-kde thumbnail plugin for Dolphin

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
