# 3mfthumb Windows shell extension

`3mfthumb.exe` (the tool in the parent directory) is a freedesktop-style
thumbnailer: file managers like `pcmanfm`/`nautilus` shell out to it and
read the PNG it writes. Windows Explorer has no equivalent "run this exe
and read its output" mechanism -- it only calls a registered in-process
COM object that implements `IThumbnailProvider`. This directory is that
COM object: a small DLL that extracts the same embedded 3MF thumbnail
(via lib3mf) and hands it to Explorer as an `HBITMAP`.

It registers itself under `HKEY_CURRENT_USER\Software\Classes`, so no
administrator rights are needed.

## Build (cross-compile from Linux with mingw-w64)

```
sudo apt install g++-mingw-w64-x86-64-win32

# Grab the lib3mf SDK (headers + prebuilt lib3mf.dll) -- same one used to
# build 3mfthumb.exe itself, see ../README.md.
cd ..
wget https://github.com/3MFConsortium/lib3mf/releases/download/v2.5.0/lib3mf_sdk_v2.5.0.zip
unzip lib3mf_sdk_v2.5.0.zip -d lib3mf_sdk

cd win-shellext
x86_64-w64-mingw32-g++ -std=c++17 -Wall -O2 \
  -I../lib3mf_sdk/lib3mf_sdk/Bindings/CppDynamic \
  -c ThumbnailProvider.cpp -o ThumbnailProvider.o

x86_64-w64-mingw32-g++ -shared -o 3mfthumbprovider.dll \
  ThumbnailProvider.o ThumbnailProvider.def \
  -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -lwindowscodecs \
  -static-libgcc -static-libstdc++
```

Notes on why the build looks the way it does:

- **`lib3mf_dynamic.hpp` (`CppDynamic` binding), not `lib3mf_implicit.hpp`.**
  A DLL's static import-table dependencies (which is how `-l3mf` would
  link it) are resolved by the OS loader *before* the DLL's own code ever
  runs. If `lib3mf.dll` isn't already somewhere on the default search
  path -- and it usually isn't, if it's just sitting next to this DLL in
  a per-user install folder -- the whole provider DLL fails to load with
  `ERROR_MODULE_NOT_FOUND`, before any of our code gets a chance to fix
  the search path. The dynamic binding sidesteps this: it loads
  `lib3mf.dll` itself, at runtime, from an absolute path computed next to
  this DLL (`GetLib3mfDllPath()` in `ThumbnailProvider.cpp`).
- **`-static-libgcc -static-libstdc++`.** mingw's default dynamic
  runtime linking pulls in `libstdc++-6.dll`, which stock Windows
  doesn't ship.
- **`IInitializeWithStream`, not just `IInitializeWithFile`.** Explorer's
  thumbnail cache (`IShellItemImageFactory::GetImage`) only ever queries
  for `IInitializeWithStream` in practice, even though
  `IInitializeWithFile` is also implemented here (and is what a caller
  using `IThumbnailProvider` directly, e.g. via `CoCreateInstance`, would
  typically use). Confirmed empirically, not from documentation --
  without stream support the extraction silently fails with
  `WTS_E_FAILEDEXTRACTION` and Explorer just shows the generic icon.

## Install

Copy `3mfthumbprovider.dll` and `lib3mf.dll` (from
`lib3mf_sdk/lib3mf_sdk/Bin/lib3mf.dll`) to wherever you want them to live
permanently, then register:

```
regsvr32 3mfthumbprovider.dll
```

`DllRegisterServer` writes only to `HKEY_CURRENT_USER\Software\Classes`,
so this doesn't need an elevated prompt. `regsvr32 /u 3mfthumbprovider.dll`
undoes it.
