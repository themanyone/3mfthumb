# AGENTS.md

Guidance for working in this repository.

## What this project is

`3mfthumb` is a small single-file C++ program that extracts the **embedded
thumbnail attachment** from a 3MF file and writes it out as a PNG. It is a
[freedesktop.org thumbnailer](https://wiki.archlinux.org/title/File_manager_functionality#Thumbnailers):
file managers (`pcmanfm`, `nautilus`, etc.) shell out to it as
`3mfthumb %i %o` when they need a preview for a `.3mf` file. On Windows,
Explorer instead loads a registered COM shell extension
(`win-shellext/ThumbnailProvider.cpp`) that implements `IThumbnailProvider`.

The program first pulls the pre-embedded thumbnail that slicer/design tools
stored in the package. If there is none, it renders a flat-shaded PNG of the
build meshes with Cairo (see "Architecture / control flow" below).

## Commands

- `make` - build `3mfthumb` (Linux). Uses `pkg-config --cflags --libs lib3mf` to locate the Lib3MF headers (`lib3mf_implicit.hpp`) and libs. Deps: `lib3mf`, `libzip`, `zlib`.
- `make install` / `make uninstall` - **must be root**; without root they print why (Nautilus' bubblewrap sandbox cannot exec a thumbnailer from a home directory and freezes instead of showing no preview) and `exit 1` (`REFUSE_UNPRIVILEGED`). Both install `DESTDIRS=$(PREFIX)` (default `/usr`: `/usr/bin/3mfthumb`, `/usr/share/thumbnailers/3mf.thumbnailer`), never touch a home directory or a user cache, and finish with `NAUTILUS_RESTART`, which drops root via `setpriv` to `$SUDO_USER` (or the first `user` login session) and runs `nautilus -q` against `/run/user/<uid>/bus`; with no session it prints the command for the user instead. `/usr/bin` is required, not merely tidy: GNOME's thumbnail sandbox only mounts `/usr` and execs the program by name through the caller's `PATH`. `install -D` needs the file as the final argument (`-D src dir/file`, never `-D src dir/`).
- `make clean`
- **`make rpm`** (Fedora) - builds binary and source RPMs from `3mfthumb.spec` via `rpmbuild` (needs `rpm-build` + `spectool`). **`make deb`** (Debian/Ubuntu) - runs `make-deb.sh`, which compiles with pkg-config flags and assembles the package with plain `dpkg-deb`, so no `debhelper`/`dpkg-dev` is required. The version is read from `3mfthumb.spec` and the revision from `debian/changelog` (override with `VERSION=`/`REVISION=`); the `.deb` is written to the current directory. `debian/` is also a full `debhelper` source package whose `rules` builds through upstream's Makefile and stages files itself (no `make install`, since that targets a home prefix). Only the `make-deb.sh` path has been exercised (on Fedora); `dpkg-buildpackage` needs a Debian host.
- `./3mfthumb <file.3mf> <out.png>` - run manually (see CLI contract below).
- **Windows cross-compile**: enter the mingw environment (`mingw64-env`) then `mingw64-make CFLAGS=-I./lib3mf_sdk/Bindings/Cpp LDFLAGS="-l3mf -lzip -lz -L./lib3mf_sdk/Lib"` to produce `3mfthumb.exe`. Needs `mingw64-gcc-g++`, `mingw64-zlib`, `mingw64-libzip` and the `lib3mf.dll` from the SDK zip.
- **CMake alternative**: `CMakeLists.txt` is a unified build for both Linux and Windows, **not tracked in git**; the Makefile is the primary build. On Windows it uses the bundled `lib3mf_sdk/` include dir, the `lib3mf_sdk/Lib/lib3mf.lib` import lib, links `zip z 3mf`, and copies `lib3mf_sdk/Bin/lib3mf.dll` next to the exe. On Linux/macOS it finds the distro lib3mf via pkg-config. Cross-build: `cmake -S . -B build-win -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++` then `cmake --build build-win`.
- **Windows shell extension build**: cross-compile `win-shellext/ThumbnailProvider.cpp` with mingw-w64. See `win-shellext/README.md`; it uses the `CppDynamic` binding (`lib3mf_dynamic.hpp`) rather than `lib3mf_implicit.hpp`, and links `-lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -lwindowscodecs`. Register with `regsvr32` (per-user, HKCU only).

- **Packaging targets version 1.2.2** (tagged `v1.2.2`): `3mfthumb.spec`, `debian/changelog` and the `Source0`/`%{version}` in the spec all need bumping together for a new release.

There is **no test suite**. Verification is manual: run `./3mfthumb some.3mf out.png` and inspect the output PNG (e.g. `file out.png`). The SDK examples under `lib3mf_sdk/Examples/Files/` (e.g. `Helix.3mf`) have no package thumbnail, so they produce no output; to test a hit, add `Metadata/thumbnail.png` plus a thumbnail relationship to the package rels.

## CLI contract

`3mfthumb [-r] [-c] [-s N] <file.3mf|file.stl> [output.png]`. Options come
before the filenames (`--` ends them); unknown options print an error and
return `1`. Without an output path, `-c` makes the cache entry the only
destination, so usage is printed when neither is available (`argc < 3` on
non-Windows). `-c/--cache` additionally writes the image to the freedesktop
thumbnail cache, named `<md5 of "file://<absolute path>">.png` under
`$XDG_CACHE_HOME/thumbnails` (or `~/.cache/thumbnails`) in the `normal` (<=128),
`large` (<=256) or `x-large` group for `-s N/--size N` (default 256, clamped to
16..4096). That is the KDE thumbnail contract: KDE has no external-plugin
thumbnailer and loads the shared library named by
`/usr/share/kservices6/3mfthumbnail.desktop` (`X-KDE-Library=3mfthumbnail`),
so that registration only works once a plugin by that name exists. The cache key
is computed at runtime, and failures to create/copy are silent (exit code still
`0`), matching the rest of the thumbnailer behaviour.
STL input (detected by a `.stl` extension, case-insensitive) bypasses Lib3MF
and goes straight to `threemf::render::renderSTLToRGBA()`, which parses binary
(84-byte header + count, size-checked) or ASCII STL; there is no package
thumbnail to extract, so `-r` is implicit.
The input may also be a `file://` URI (percent-escapes are decoded), because
GNOME-based file managers hand thumbnailers a URI via `%i` from inside a
bubblewrap sandbox that only binds `/usr`, `/etc`, `/proc`, `/dev`.
Errors go to stderr prefixed with `3mfthumb: `.

**Gotcha:** the program returns exit code `0` even when the 3MF has *no*
package thumbnail and therefore writes *nothing*. Do not rely on the exit code
or a guaranteed output file - a missing output file can be a success. Only the
existence of a non-empty `<output.png>` indicates a thumbnail was found.

`lib3mf_sdk/Examples/Files/Helix.3mf` has no package thumbnail, so with `-r`
it exercises the fallback renderer (a 256x256 PNG is written); without `-r`
the renderer writes it too, since rendering is the fallback for missing
previews.

## Architecture / control flow

The core tool lives in one file, `3mfthumb.cpp` (~170 lines):

1. Validate args.
2. `Lib3MF::CWrapper::loadLibrary()` - loads Lib3MF. This is the **implicit
   API** header (`lib3mf_implicit.hpp`), which resolves the C library symbols
   at runtime (not a compile-time link of each call). Requires the shared lib
   to be loadable at runtime, not just present at link time.
3. `wrapper->CreateModel()` -> `model->QueryReader("3mf")` -> `reader->ReadFromFile(filename)`.
4. `model->HasPackageThumbnailAttachment()` then `GetPackageThumbnailAttachment()`, then `thumbnail->WriteToFile(thumbnailFilename)`.
5. All Lib3MF calls throw `std::exception` on error; caught in `main`, message printed, returns `1`.
6. Fallback: when there is no package thumbnail (or `-r`), `renderThumbnail()`
   calls `threemf::render::renderToRGBA()` from `win-shellext/renderer.h` and
   wraps it in a Cairo surface (Linux) or `writePNG()` (Windows). Render
   errors are printed but do not change the exit code. `renderFacesToRGBA()`
   is the shared drawing tail used by both the 3MF and STL entry points.

The SDK (bundled under `lib3mf_sdk/`, version 2.4.1) is the reference for the
API; the example sources under `lib3mf_sdk/Examples/Cpp/Source/*.cpp` show the
patterns (e.g. `ExtractInfo.cpp`).

## Gotchas and non-obvious things

- **C++17 is required** (`#include <filesystem>`; there is an `#error` for older standards). The Makefile does **not** set `-std=` explicitly - it relies on the compiler default (modern gcc defaults to gnu++17). `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 17`. Don't "fix" the Makefile by dropping `-O2`/`-g`; if a build fails on an old compiler, add `-std=c++17`, don't remove the filesystem dependency.
- **`lib3mf_implicit.hpp` is never in the repo** (when using the distro package) - it comes from `lib3mf-devel` at `/usr/include/lib3mf/`, discovered via pkg-config. clangd/LSP will report `pp_file_not_found: 'lib3mf_implicit.hpp'` and cascade "undeclared identifier `Lib3MF`" errors because it doesn't use pkg-config's include path. These are expected, not real build failures. `win-shellext/ThumbnailProvider.cpp` is different: a scoped `win-shellext/.clangd` points clangd at the mingw-w64 sysroot and the bundled `CppDynamic` headers, so its diagnostics are real (requires the mingw-w64 toolchain installed).
- **Arch Linux packaging bug**: `lib3mf` may install without symlinks to the C bindings; workaround is `ln -s /usr/include/lib3mf/Bindings/Cpp/* /usr/include/lib3mf/`.
- **If pkg-config is missing**, the README documents substituting `BUILD_CFLAGS:=-I/usr/include/lib3mf` and `LDFLAGS:=-l3mf -lzip -lz` on the `make` command line.
- **Windows two-arg mode**: under `#ifdef WIN32` one argument is always enough, so `3mfthumb <file.3mf>` (no output path) writes the thumbnail where Explorer expects a one-argument invocation; on other builds one argument is only accepted together with `-c`.
- **The thumbnailer cache**: after `make install`, existing stale thumbnails block new previews. The Makefile does not wipe `/home/*/.cache/thumbnails` (root must not touch a user's cache), so document `rm -rf $HOME/.cache/thumbnails/*`; a failed `.3mf` keeps a marker under `~/.cache/thumbnails/fail/gnome-thumbnail-factory/` until the cache is cleared. File managers may need "previews for files > 1MB" enabled.
- **Never ship a Nautilus GResource overlay**: tracing `openat` shows Nautilus 49 never opens `~/.local/lib64`; it only loads the three `.so` extensions in `/usr/lib64/nautilus/extensions-4`. A stray `nautilus-thumbnailers.gresource` there made Nautilus hang on startup (killed by the session). The old `install-nautilus` target and the copies in `3mfthumb.spec`, `debian/rules` and `make-deb.sh` were removed for this reason; the system-wide `/usr/share/thumbnailers` entry is the fix.
- **The GNOME thumbnail sandbox command** (captured from gnome-desktop's `bwrap` argv) is `bwrap --ro-bind /usr /usr --ro-bind-try /etc/ld.so.cache --symlink /usr//bin /bin --symlink /usr//lib64 /lib64 --symlink /usr//lib /lib --symlink /usr//sbin /sbin --proc /proc --dev /dev --chdir / --setenv GIO_USE_VFS local --unshare-all --die-with-parent --bind <tmpdir> /tmp --ro-bind <file> /tmp/gnome-desktop-file-to-thumbnail.3mf --seccomp N 3mfthumb /tmp/gnome-desktop-file-to-thumbnail.3mf /tmp/gnome-desktop-thumbnailer.png`. Only `argv[0]` is resolved through the caller's `PATH`, `/home`, `/etc` and `/usr/local` are not mounted, the input is a path (not a URI) and no `.thumbnailer` key disables the sandbox. Reproduce by hand without seccomp: `bwrap --ro-bind /usr /usr ... --unshare-all 3mfthumb <file> /tmp/o.png`; with the binary in `/usr/bin` it exits 0.
- **Nautilus thumbnails are unreliable to debug in-process.** `Gio.File.query_info_async("thumbnail::*")` never spawns a thumbnailer here, and a Nautilus started from a test harness (PTY, `--new-window`, or a `dbus-run-session`) does not generate thumbnails at all, not even for PNGs, because GIO refuses it without a registered application (printed as `Failed to register: Timeout was reached`). Only a real desktop session is authoritative: wipe `~/.cache/thumbnails`, restart Nautilus and look for a `<md5 of file URI>` file under `~/.cache/thumbnails/large/`.
- **Two thumbnailers for one MIME type is undefined**: distros ship `stl-thumb` claiming `model/3mf`. With both registered, Nautilus may sit on a slow GPU renderer; remove the 3MF claim from `stl-thumb.thumbnailer`.
- **Mime types**: `3mf.thumbnailer` and `kde-shellext/3mfthumbnail.json` advertise `model/3mf`, `application/vnd.ms-3mfdocument`, `model/stl` and `application/sla`. If a file manager still doesn't pick up `.3mf`, the mime registration is usually the issue, not the binary.
- **KDE has no external-program thumbnailer**: KIO only loads shared libraries from the `thumbcreator` plugin directory, so "add a `.thumbnailer` for Dolphin" never works; the plugin (or a `-c` run from a file manager's custom action) is the only route.
- **Do not commit build artifacts or vendored SDK**: `.gitignore` now covers `*.o`, `*.exe`, `*.dll`, `build-win/`, `build-kde/` and `lib3mf_sdk/`, so those stay untracked. The `lib3mf_sdk/` directory is a manual download (`lib3mf_sdk_v2.4.1.zip` from the lib3mf releases page), not a submodule. Tracked files include `3mfthumb.cpp`, `Makefile`, `3mf.thumbnailer`, `README.md`, `LICENSE`, `preview.png`, `.gitignore`, `win-shellext/`, `kde-shellext/`, `3mfthumb.spec`, `make-deb.sh`, and `debian/`. `.deb` files and the `debian/3mfthumb/` staging dir are ignored. Note: `CMakeLists.txt` and `CMakeLists.txt.linux` are still **not** tracked.
- **Windows shell extension is now on `main`**: the `win-shellext/` COM extension (originally from the `ksuquix-forks/main` fork, merged upstream as PR #8) was folded into this repo's `main` as a regular tracked directory, replacing the old registry-exe approach in the README. The real mechanism is an in-process `IThumbnailProvider` COM object, not a shelled-out executable. Don't regress the README back to the regedit/`ThumbnailHandler`-pointing-at-exe advice.
- **KDE plugin** (`kde-shellext/`, CMake only, not part of the Makefile's own compile): `thumbcreator.cpp` is a `KIO::ThumbnailCreator` (`K_PLUGIN_CLASS_WITH_JSON`) linking Qt6 Core/Gui, `KF6::KIOGui` and lib3mf through pkg-config; `3mfthumbnail.json` declares `ThumbCreator` + `CacheThumbnail`. Fedora ships no KF6 umbrella CMake config, so the CMake file uses `find_package(KF6KIO)`; it installs to `KDE_INSTALL_PLUGINDIR/thumbcreator` when ECM is present, else `${libdir}/qt6/plugins/thumbcreator`. It returns the package thumbnail when Qt decodes it, else renders through `threemf::render`. `make install`, the spec, `debian/rules` and `make-deb.sh` build it optionally (skipped when cmake/Qt6/KF6 KIO are absent). Packaging keeps it separate as `3mfthumb-kde`: an RPM subpackage gated by `%bcond_without kde` (`--without kde` drops it; `rpmspec -P --without kde` shows no `%files kde`, but `rpmspec -q` still lists the subpackage, so check the parsed spec, not the query) and a second package in `debian/control` for debhelper, while `make-deb.sh` builds both `.deb`s and derives the plugin's `Depends` from `ldd` minus the base binary's libraries (duplicate Depends lines are invalid). The spec and debhelper rules only copy `kde-shellext/` sources into `%doc`. Verify by loading the built `.so` with `QPluginLoader`, `KPluginFactory::create()`, then `ThumbnailCreator::create(KIO::ThumbnailRequest(url, size, mime, dpr, seq))`; PyQt6 here has no KIO bindings.
- **GPLv2** licensed; every source file carries the GPL header. Match the existing `///` comment style and keep attribution lines.

## Style / conventions

- Single translation unit for the core tool, plus the self-contained Windows-only `win-shellext/` translation unit. Keep it simple.
- `///` doc-comment style in the source header.
- Commit history mixes conventional subjects with `Co-Authored-By:` trailers and PR merge commits. Author: Henry Kroll III (`nospam@thenerdshow.com`).
