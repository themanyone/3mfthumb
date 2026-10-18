#!/bin/sh
set -eu

# Builds Debian packages without debhelper or dpkg-dev. Needs dpkg-deb,
# pkg-config, g++ and the lib3mf/libzip/zlib/cairo development headers.
# 3mfthumb-kde (the KDE thumbnail plugin) is added when cmake, Qt 6 and
# KF6 KIO are installed; the base package never depends on them.

NAME=3mfthumb
KDE_NAME=$NAME-kde
SRCDIR=$(cd "$(dirname "$0")" && pwd)
VERSION=${VERSION:-$(sed -n 's/^Version:[[:space:]]*//p' "$SRCDIR/$NAME.spec")}
REVISION=${REVISION:-$(sed -n "s/^$NAME (.*-\([0-9][0-9]*\)).*/\1/p" "$SRCDIR/debian/changelog" | head -n 1)}
ARCH=${ARCH:-$(dpkg --print-architecture)}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

BUILD_CFLAGS=$(pkg-config --cflags lib3mf cairo)
BUILD_LIBS=$(pkg-config --libs lib3mf cairo)

${CXX:-g++} -std=c++17 -O2 -Wall $BUILD_CFLAGS "$SRCDIR/$NAME.cpp" \
	-o "$WORK/$NAME" $BUILD_LIBS

ROOT=$WORK/root
install -Dm 755 "$WORK/$NAME" "$ROOT/usr/bin/$NAME"
install -Dm 644 "$SRCDIR/3mf.thumbnailer" \
	"$ROOT/usr/share/thumbnailers/3mf.thumbnailer"

# KDE thumbnail plugin, packaged separately so its Qt/KIO dependencies do not
# land on every system.
# Shared library names a binary resolves at run time ("libfoo.so.1" ->
# "libfoo.so").
sonames() {
	ldd "$1" 2>/dev/null |
		sed -n 's/^[[:space:]]*\([^ ]*\) => .*/\1/p' | sed 's/\.so.*/\.so/' | sort -u
}

KDE_ROOT=$WORK/kde-root
KDE_LIBS=""
if command -v cmake >/dev/null 2>&1 && \
	cmake -S "$SRCDIR/kde-shellext" -B "$WORK/build-kde" \
		-DCMAKE_INSTALL_PREFIX=/usr >/dev/null 2>&1; then
	cmake --build "$WORK/build-kde" >/dev/null
	cmake --install "$WORK/build-kde" --prefix "$KDE_ROOT/usr" >/dev/null
	# Libraries the plugin needs beyond those the base package already pulls
	# in through its own binary (which is why dpkg-shlibdeps is not needed
	# here).
	KDE_LIB=$(find "$KDE_ROOT" -name 'lib3mfthumbnail.so')
	comm -23 <(sonames "$KDE_LIB") <(sonames "$WORK/$NAME") > "$WORK/kde-libs"
	# One comma-separated Depends line.
	KDE_LIBS=$(tr '\n' ',' < "$WORK/kde-libs" | sed 's/,$//')
fi
mkdir -p "$ROOT/DEBIAN"

INSTALLED_SIZE=$(du -sk "$ROOT/usr" | cut -f1)
cat > "$ROOT/DEBIAN/control" <<EOF
Package: $NAME
Version: $VERSION-$REVISION
Architecture: $ARCH
Maintainer: Henry Kroll III <nospam@thenerdshow.com>
Installed-Size: $INSTALLED_SIZE
Section: graphics
Priority: optional
Depends: lib3mf1, libzip5 | libzip4, zlib1g, libcairo2
Homepage: https://github.com/themanyone/3mfthumb
Description: thumbnail generator for 3D Manufacturing Format files
 3mfthumb extracts the thumbnail image that slicer and design applications
 embed in a 3MF package and writes it out as a PNG. It implements the
 freedesktop.org thumbnailer interface, so file managers such as Nautilus,
 Thunar and PCManFM can show previews of .3mf files. When a package has no
 embedded image, the build meshes are rendered instead.
EOF

dpkg-deb --root-owner-group --build "$ROOT" "${NAME}_${VERSION}-${REVISION}_${ARCH}.deb"
echo "Built ${NAME}_${VERSION}-${REVISION}_${ARCH}.deb"

if [ -n "$KDE_LIBS" ]; then
	mkdir -p "$KDE_ROOT/DEBIAN"
	KDE_INSTALLED_SIZE=$(du -sk "$KDE_ROOT/usr" | cut -f1)
	cat > "$KDE_ROOT/DEBIAN/control" <<EOF
Package: $KDE_NAME
Version: $VERSION-$REVISION
Architecture: $ARCH
Maintainer: Henry Kroll III <nospam@thenerdshow.com>
Installed-Size: $KDE_INSTALLED_SIZE
Section: graphics
Priority: optional
Depends: $NAME (= $VERSION-$REVISION), $KDE_LIBS
Homepage: https://github.com/themanyone/3mfthumb
Description: KDE Dolphin thumbnail plugin for 3MF and STL files
 KIO thumbnail plugin that shows previews of 3MF and STL files in KDE
 applications such as Dolphin and the file dialog. KDE does not read
 freedesktop .thumbnailer entries, so the 3mfthumb binary alone is not
 enough there. Separate from 3mfthumb so the Qt and KIO dependencies stay
 off non-KDE systems.
EOF
	dpkg-deb --root-owner-group --build "$KDE_ROOT" \
		"${KDE_NAME}_${VERSION}-${REVISION}_${ARCH}.deb"
	echo "Built ${KDE_NAME}_${VERSION}-${REVISION}_${ARCH}.deb"
else
	echo "Skipped $KDE_NAME (needs cmake, Qt 6 and KF6 KIO to build)" >&2
fi
