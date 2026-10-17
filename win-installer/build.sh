#!/bin/sh
# build.sh - cross-compile the Windows binaries and build the NSIS installer.
#
# Requires:
#   - g++-mingw-w64-x86-64 (x86_64-w64-mingw32-g++)
#   - mingw64-libzip, mingw64-zlib (mingw import libs for the CLI build)
#   - makensis (NSIS 3.x, the "mingw64-nsis" package provides amd64 stubs)
#   - the lib3mf SDK extracted to ../lib3mf_sdk with lib3mf.dll in
#     lib3mf_sdk/Bin and lib3mf.lib in lib3mf_sdk/Lib (same SDK used to
#     cross-compile 3mfthumb.exe; see ../README.md)
#
# Output: ./3mfthumb-setup.exe
#
# Copyright 2026 Don Eisele <https://toysmakeuspowerful.com/>
# This is free software under the GPLv2; see ../LICENSE.

set -e

cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"
SDK="$ROOT/lib3mf_sdk"

CXX="${CXX:-x86_64-w64-mingw32-g++}"
NSIS="${NSIS:-makensis}"
MINGW_BIN="$($CXX -print-sysroot)/mingw/bin"
WINPTHREAD="$MINGW_BIN/libwinpthread-1.dll"

if [ ! -d "$SDK/Bindings/CppDynamic" ] || [ ! -f "$SDK/Bin/lib3mf.dll" ]; then
  echo "error: lib3mf SDK not found under $SDK" >&2
  echo "Get it from https://github.com/3MFConsortium/lib3mf/releases and extract here." >&2
  exit 1
fi

command -v "$CXX" >/dev/null 2>&1 || { echo "error: $CXX not found (need mingw-w64 g++)" >&2; exit 1; }
command -v "$NSIS" >/dev/null 2>&1 || { echo "error: $NSIS not found (need NSIS / makensis)" >&2; exit 1; }

echo "==> Building 3mfthumbprovider.dll (Explorer shell extension)"
(cd "$ROOT/win-shellext" && \
  "$CXX" -std=c++17 -Wall -O2 -I"$SDK/Bindings/CppDynamic" \
    -c ThumbnailProvider.cpp -o ThumbnailProvider.o && \
  "$CXX" -shared -o 3mfthumbprovider.dll \
    ThumbnailProvider.o ThumbnailProvider.def \
    -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -lwindowscodecs \
    -static-libgcc -static-libstdc++)

# The CLI is optional: it needs mingw libzip/zlib, which may not be
# installed. If it fails to build, the installer still works (it just
# ships the Explorer shell extension, which is the part Explorer uses).
HAVE_CLI=0
echo "==> Building 3mfthumb.exe (command-line tool)"
if [ ! -f "$WINPTHREAD" ]; then
  echo "warning: $WINPTHREAD not found; skipping CLI build." >&2
elif (cd "$ROOT" && \
  "$CXX" -std=c++17 -Wall -O2 -I"$SDK/Bindings/Cpp" \
    -I"$SDK/Bindings/CppDynamic" \
    -c 3mfthumb.cpp -o 3mfthumb-win.o && \
  "$CXX" 3mfthumb-win.o -o 3mfthumb.exe \
    -L"$SDK/Lib" -l3mf -lzip -lz \
    -static-libgcc -static-libstdc++); then
  HAVE_CLI=1
else
  echo "warning: 3mfthumb.exe build failed (need mingw64-libzip and" >&2
  echo "         mingw64-zlib); the installer will ship only the shell" >&2
  echo "         extension." >&2
fi

echo "==> Staging installer payload"
mkdir -p staging
cp "$ROOT/win-shellext/3mfthumbprovider.dll" staging/
cp "$SDK/Bin/lib3mf.dll" staging/
if [ "$HAVE_CLI" = 1 ]; then
  cp "$ROOT/3mfthumb.exe" staging/
  cp "$WINPTHREAD" staging/
fi

echo "==> Building 3mfthumb-setup.exe"
if [ "$HAVE_CLI" = 1 ]; then
  "$NSIS" -V2 -DHAS_CLI 3mfthumb.nsi
else
  "$NSIS" -V2 3mfthumb.nsi
fi

echo "==> Done: $(pwd)/3mfthumb-setup.exe"
ls -lh 3mfthumb-setup.exe
