#!/bin/sh
set -eu

# Builds a Debian package without debhelper or dpkg-dev. Needs dpkg-deb,
# pkg-config, g++ and the lib3mf/libzip/zlib development headers.

NAME=3mfthumb
SRCDIR=$(cd "$(dirname "$0")" && pwd)
VERSION=${VERSION:-$(sed -n 's/^Version:[[:space:]]*//p' "$SRCDIR/$NAME.spec")}
REVISION=${REVISION:-$(sed -n "s/^$NAME (.*-\([0-9][0-9]*\)).*/\1/p" "$SRCDIR/debian/changelog" | head -n 1)}
ARCH=${ARCH:-$(dpkg --print-architecture)}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

BUILD_CFLAGS=$(pkg-config --cflags lib3mf)
BUILD_LIBS=$(pkg-config --libs lib3mf)

${CXX:-g++} -std=c++17 -O2 -Wall $BUILD_CFLAGS "$SRCDIR/$NAME.cpp" \
	-o "$WORK/$NAME" $BUILD_LIBS

ROOT=$WORK/root
install -Dm 755 "$WORK/$NAME" "$ROOT/usr/bin/$NAME"
install -Dm 644 "$SRCDIR/3mf.thumbnailer" \
	"$ROOT/usr/share/thumbnailers/3mf.thumbnailer"
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
Depends: lib3mf1, libzip5 | libzip4, zlib1g
Homepage: https://github.com/themanyone/3mfthumb
Description: thumbnail generator for 3D Manufacturing Format files
 3mfthumb extracts the thumbnail image that slicer and design applications
 embed in a 3MF package and writes it out as a PNG. It implements the
 freedesktop.org thumbnailer interface, so file managers such as Nautilus,
 Thunar and PCManFM can show previews of .3mf files. The 3D model itself is
 never rendered.
EOF

dpkg-deb --root-owner-group --build "$ROOT" "${NAME}_${VERSION}-${REVISION}_${ARCH}.deb"
echo "Built ${NAME}_${VERSION}-${REVISION}_${ARCH}.deb"
