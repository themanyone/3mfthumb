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

The actual 3D model is never rendered; the program only pulls the pre-embedded
thumbnail that slicer/design tools stored in the package.

## Commands

- `make` - build `3mfthumb` (Linux). Uses `pkg-config --cflags --libs lib3mf` to locate the Lib3MF headers (`lib3mf_implicit.hpp`) and libs. Deps: `lib3mf`, `libzip`, `zlib`.
- `make install` - installs the binary and `3mf.thumbnailer` to `$HOME/.local/bin` and `$HOME/.local/share/thumbnailers/`, then **deletes the user's entire thumbnail cache** (`~/.cache/thumbnails/*`). If run as root it installs to `/usr/local` instead (`INSTALL_ROOT` vs `INSTALL_HOME` logic in the Makefile).
- `make uninstall` / `make clean`
- `./3mfthumb <file.3mf> <out.png>` - run manually (see CLI contract below).
- **Windows cross-compile**: enter the mingw environment (`mingw64-env`) then `mingw64-make CFLAGS=-I./lib3mf_sdk/Bindings/Cpp LDFLAGS="-l3mf -lzip -lz -L./lib3mf_sdk/Lib"` to produce `3mfthumb.exe`. Needs `mingw64-gcc-g++`, `mingw64-zlib`, `mingw64-libzip` and the `lib3mf.dll` from the SDK zip.
- **CMake alternative**: `CMakeLists.txt` is a unified build for both Linux and Windows, **not tracked in git**; the Makefile is the primary build. On Windows it uses the bundled `lib3mf_sdk/` include dir, the `lib3mf_sdk/Lib/lib3mf.lib` import lib, links `zip z 3mf`, and copies `lib3mf_sdk/Bin/lib3mf.dll` next to the exe. On Linux/macOS it finds the distro lib3mf via pkg-config. Cross-build: `cmake -S . -B build-win -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++` then `cmake --build build-win`.
- **Windows shell extension build**: cross-compile `win-shellext/ThumbnailProvider.cpp` with mingw-w64. See `win-shellext/README.md`; it uses the `CppDynamic` binding (`lib3mf_dynamic.hpp`) rather than `lib3mf_implicit.hpp`, and links `-lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -lwindowscodecs`. Register with `regsvr32` (per-user, HKCU only).

There is **no test suite**. Verification is manual: run `./3mfthumb some.3mf out.png` and inspect the output PNG (e.g. `file out.png`).

## CLI contract

`3mfthumb <file.3mf> <output.png>`, `argc < 3` prints usage and returns `1`.

**Gotcha:** the program returns exit code `0` even when the 3MF has *no*
package thumbnail and therefore writes *nothing*. Do not rely on the exit code
or a guaranteed output file - a missing output file can be a success. Only the
existence of a non-empty `<output.png>` indicates a thumbnail was found.

A good no-thumbnail test case is `lib3mf_sdk/Examples/Files/Helix.3mf` - it
silently produces no output.

## Architecture / control flow

Everything lives in one file, `3mfthumb.cpp` (~67 lines):

1. Validate args.
2. `Lib3MF::CWrapper::loadLibrary()` - loads Lib3MF. This is the **implicit
   API** header (`lib3mf_implicit.hpp`), which resolves the C library symbols
   at runtime (not a compile-time link of each call). Requires the shared lib
   to be loadable at runtime, not just present at link time.
3. `wrapper->CreateModel()` -> `model->QueryReader("3mf")` -> `reader->ReadFromFile(filename)`.
4. `model->HasPackageThumbnailAttachment()` then `GetPackageThumbnailAttachment()`, then `thumbnail->WriteToFile(thumbnailFilename)`.
5. All Lib3MF calls throw `std::exception` on error; caught in `main`, message printed, returns `1`.

The SDK (bundled under `lib3mf_sdk/`, version 2.4.1) is the reference for the
API; the example sources under `lib3mf_sdk/Examples/Cpp/Source/*.cpp` show the
patterns (e.g. `ExtractInfo.cpp`).

## Gotchas and non-obvious things

- **C++17 is required** (`#include <filesystem>`; there is an `#error` for older standards). The Makefile does **not** set `-std=` explicitly - it relies on the compiler default (modern gcc defaults to gnu++17). `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 17`. Don't "fix" the Makefile by dropping `-O2`/`-g`; if a build fails on an old compiler, add `-std=c++17`, don't remove the filesystem dependency.
- **`lib3mf_implicit.hpp` is never in the repo** (when using the distro package) - it comes from `lib3mf-devel` at `/usr/include/lib3mf/`, discovered via pkg-config. clangd/LSP will report `pp_file_not_found: 'lib3mf_implicit.hpp'` and cascade "undeclared identifier `Lib3MF`" errors because it doesn't use pkg-config's include path. These are expected, not real build failures. `win-shellext/ThumbnailProvider.cpp` is different: a scoped `win-shellext/.clangd` points clangd at the mingw-w64 sysroot and the bundled `CppDynamic` headers, so its diagnostics are real (requires the mingw-w64 toolchain installed).
- **Arch Linux packaging bug**: `lib3mf` may install without symlinks to the C bindings; workaround is `ln -s /usr/include/lib3mf/Bindings/Cpp/* /usr/include/lib3mf/`.
- **If pkg-config is missing**, the README documents substituting `BUILD_CFLAGS:=-I/usr/include/lib3mf` and `LDFLAGS:=-l3mf -lzip -lz` on the `make` command line.
- **Dead Windows branch**: inside `#ifdef WIN32` the code checks `if (argc == 2)`, but the earlier guard (`argc < 3` -> return 1) makes `argc == 2` unreachable, so that whole block is dead code as written. Don't be confused by it; it appears to be a leftover from an earlier two-arg CLI design.
- **The thumbnailer cache**: after `make install`, existing stale thumbnails block new previews. The Makefile already wipes `~/.cache/thumbnails/*`, but after manual testing you may need to do the same (`rm -rf $HOME/.cache/thumbnails/*`), and file managers may need "previews for files > 1MB" enabled.
- **Mime types**: `3mf.thumbnailer` advertises `MimeType=model/3mf;application/vnd.ms-3mfdocument;`. If a file manager still doesn't pick up `.3mf`, the mime registration is usually the issue, not the binary.
- **Do not commit build artifacts or vendored SDK**: `.gitignore` now covers `*.o`, `*.exe`, `*.dll`, `build-win/`, and `lib3mf_sdk/`, so those stay untracked. The `lib3mf_sdk/` directory is a manual download (`lib3mf_sdk_v2.4.1.zip` from the lib3mf releases page), not a submodule. Tracked files: `3mfthumb.cpp`, `Makefile`, `3mf.thumbnailer`, `README.md`, `LICENSE`, `preview.png`, `.gitignore`, and `win-shellext/`. Note: `CMakeLists.txt` and `CMakeLists.txt.linux` are still **not** tracked.
- **Windows shell extension is now on `main`**: the `win-shellext/` COM extension (originally from the `ksuquix-forks/main` fork, merged upstream as PR #8) was folded into this repo's `main` as a regular tracked directory, replacing the old registry-exe approach in the README. The real mechanism is an in-process `IThumbnailProvider` COM object, not a shelled-out executable. Don't regress the README back to the regedit/`ThumbnailHandler`-pointing-at-exe advice.
- **GPLv2** licensed; every source file carries the GPL header. Match the existing `///` comment style and keep attribution lines.

## Style / conventions

- Single translation unit for the core tool, plus the self-contained Windows-only `win-shellext/` translation unit. Keep it simple.
- `///` doc-comment style in the source header.
- Commit history mixes conventional subjects with `Co-Authored-By:` trailers and PR merge commits. Author: Henry Kroll III (`nospam@thenerdshow.com`).
