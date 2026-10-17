# 3mfthumb Windows 11 installer

One-command way to get .3mf thumbnails into Windows Explorer. Builds a
per-user NSIS installer (`3mfthumb-setup.exe`) that:

1. copies `3mfthumbprovider.dll` (the COM `IThumbnailProvider` shell
   extension) and its runtime dependency `lib3mf.dll` into
   `%LOCALAPPDATA%\Programs\3mfthumb\`
2. registers the COM server under `HKEY_CURRENT_USER\Software\Classes`
   (via the DLL's `DllRegisterServer`), so **no administrator rights are
   needed**
3. optionally installs the `3mfthumb.exe` command-line tool and a Start
   Menu entry
4. installs a normal "Uninstall 3mfthumb" entry in
   Settings > Apps > Installed apps

Everything lives under `HKCU` and `%LOCALAPPDATA%`, so the whole thing is
per-user and cleanly reversible.

## Build (cross-compile from Linux)

Same toolchain as the README's Windows cross-compile section, plus NSIS.

```sh
# Fedora / RHEL-like
sudo dnf install mingw64-gcc-g++ mingw64-libzip mingw64-zlib \
                 mingw64-nsis mingw32-nsis

# Debian / Ubuntu-like
sudo apt install g++-mingw-w64-x86-64 mingw-w64-tools nsis
```

`mingw32-nsis` is needed only because some distros split the NSIS data
stubs: `mingw64-nsis` ships only the amd64 stubs and makensis's default
target is x86. `mingw32-nsis` provides the x86 stubs so makensis can
start; the installer itself is built for amd64 (see `CPU amd64` in
`3mfthumb.nsi`).

Then, from this directory:

```sh
./build.sh
```

This cross-compiles `3mfthumbprovider.dll` and `3mfthumb.exe`, stages the
payload (including `lib3mf.dll` from `../lib3mf_sdk/Bin/`), and runs
`makensis` to produce `3mfthumb-setup.exe`.

Dependencies:

- `x86_64-w64-mingw32-g++` (mingw-w64)
- `makensis` (NSIS 3.x)
- the lib3mf SDK extracted at `../lib3mf_sdk/` with `lib3mf.dll` in
  `Bin/` and `lib3mf.lib` in `Lib/` (same SDK used to build
  `3mfthumb.exe`, see `../README.md`)
- `mingw64-libzip` and `mingw64-zlib` — only for the optional
  command-line tool; if they are missing, `build.sh` prints a warning and
  ships the shell extension alone.

Output: `3mfthumb-setup.exe`.

## Install on Windows 11

Double-click `3mfthumb-setup.exe`. Accept the default install location
(`%LOCALAPPDATA%\Programs\3mfthumb`) and components, then finish. No
elevated prompt, no UAC.

Notes:

- If Explorer still shows the generic icon for .3mf files, the handler
  was likely registered after Explorer cached the old state. Restart
  Explorer (or log out and back in). For a hard refresh, close Explorer,
  delete `%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db`,
  and reopen the folder.
- The provider reads the embedded thumbnail straight from the 3MF
  package; it never renders the mesh, so it works even for huge files.
- A `.3mf` file with no embedded thumbnail simply shows the generic icon
  (the provider returns `S_FALSE`).

## Uninstall

Settings > Apps > Installed apps > 3mfthumb > Uninstall, or run
`Uninstall.exe`. This calls `DllUnregisterServer`, removes the registry
keys, the Start Menu folder, and the installed files. Silently:
`Uninstall.exe /S`.

## Files

- `3mfthumb.nsi` — the NSIS installer script
- `build.sh` — cross-compile + package script
- `staging/` — generated payload (gitignored)
- `3mfthumb-setup.exe` — generated installer (gitignored)
