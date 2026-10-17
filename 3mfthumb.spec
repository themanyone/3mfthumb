Name:           3mfthumb
Version:        1.2.1
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
BuildRequires:  glib2-devel
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
# Upstream's install target assumes a user-local prefix (DESTDIR/bin), so
# stage the files directly rather than through `make install`.
install -Dpm 755 3mfthumb %{buildroot}%{_bindir}/3mfthumb
install -Dpm 644 3mf.thumbnailer %{buildroot}%{_datadir}/thumbnailers/3mf.thumbnailer
# Nautilus 43+ reads thumbnailers from compiled GResources and ignores
# /usr/share/thumbnailers. Ship an override of the same resource name with
# only this package's entry, so sessions that load resources from
# nautilus/extensions-4 hand 3MF files to 3mfthumb. Sources are staged
# relative to the buildroot, the manifest is not installed.
if [ -d /usr/lib64/nautilus/extensions-4 ]; then
  mkdir -p %{buildroot}%{_libdir}/nautilus/extensions-4
  printf '<?xml version="1.0" encoding="UTF-8"?>\n<gresources>\n  <gresource prefix="/org/gnome/Nautilus">\n    <file alias="thumbnailers/3mf.thumbnailer">usr/share/thumbnailers/3mf.thumbnailer</file>\n  </gresource>\n</gresources>\n' \
    > %{buildroot}%{_libdir}/nautilus/extensions-4/nautilus-thumbnailers.gresource.xml
  (cd %{buildroot} && glib-compile-resources --sourcedir=. \
    --target=%{buildroot}%{_libdir}/nautilus/extensions-4/nautilus-thumbnailers.gresource \
    %{buildroot}%{_libdir}/nautilus/extensions-4/nautilus-thumbnailers.gresource.xml)
  rm -f %{buildroot}%{_libdir}/nautilus/extensions-4/nautilus-thumbnailers.gresource.xml
fi
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
%{_libdir}/nautilus/extensions-4/nautilus-thumbnailers.gresource
%license %{_docdir}/%{name}/LICENSE
%doc %{_docdir}/%{name}/source
%doc %{_docdir}/%{name}/win-installer
%doc %{_docdir}/%{name}/win-shellext
%{_bindir}/3mfthumb
%{_datadir}/thumbnailers/3mf.thumbnailer

%changelog
* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.2.1-1
- Windows shell extension renders packages without an embedded thumbnail

* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.2.0-1
- Render the meshes when a package has no embedded preview image
- Add -r/--render to force rendering, and material colours from 3MF

* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.1.1-1
- Accept file:// URIs so GNOME/Nautilus sandboxed thumbnailers work

* Sat Oct 17 2026 Henry Kroll III <nospam@thenerdshow.com> - 1.1.0-1
- Initial package for the v1.1.0 release
