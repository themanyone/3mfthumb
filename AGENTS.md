# AGENTS.md

Guidance for working in this repository.

## What this project is

`3mfthumb` shows previews of **3MF** and **STL** models in desktop file
managers. For 3MF it first extracts the **embedded thumbnail attachment** that
slicer/design tools stored in the package; when there is none (and always for
STL, which cannot carry one) it renders a flat-shaded PNG of the triangles with
its own rasteriser. On Linux it is a
[freedesktop.org thumbnailer](https://wiki.archlinux.org/title/File_manager_functionality#Thumbnailers)
that `pcmanfm`, `nautilus` and others run as `3mfthumb %i %o`; on Windows,
Explorer loads the COM shell extension in `win-shellext/` instead, and KDE
loads the `KIO::ThumbnailCreator` plugin in `kde-shellext/`. All three share
one mesh renderer, `win-shellext/renderer.h`.

## Layout

| Path | What it is |
| --- | --- |
| `3mfthumb.cpp` | The command-line tool (the freedesktop thumbnailer). |
| `win-shellext/renderer.h` | Shared renderer: 3MF flattening, STL parsing, rasteriser, PNG encoder. |
| `win-shellext/ThumbnailProvider.cpp` | Windows `IThumbnailProvider` COM extension. |
| `kde-shellext/` | KDE thumbnail plugin, built with CMake only. |
| `3mf.thumbnailer` | freedesktop thumbnailer entry (`model/3mf`, `model/stl`, ...). |
| `Makefile` | Primary build, install and packaging entry points. |
| `3mfthumb.spec`, `make-deb.sh`, `debian/` | RPM and DEB packaging. |

## Commands

- `make` - build `3mfthumb`. Uses `pkg-config --cflags --libs lib3mf cairo`.
  Deps: `lib3mf`, `libzip`, `zlib`, `cairo`. `3mfthumb.o` depends on `*.h` and
  `win-shellext/*.h`, so header edits rebuild it; `make clean` is still the
  safe reset when a build looks stale.
- `make install` / `make uninstall` - **must be root** (`ROOT_INSTALL=0` refuses
  with an explanation). Installs to `PREFIX` (default `/usr`):
  `/usr/bin/3mfthumb` and `/usr/share/thumbnailers/3mf.thumbnailer`, then tries
  the KDE plugin via CMake (built into `build-kde/`, installed into Qt's
  `thumbcreator` plugin dir under `PREFIX`), and finally restarts Nautilus.
  `/usr/bin` is required, not merely tidy: GNOME's thumbnail sandbox mounts
  only `/usr` and execs the program by name through the caller's `PATH`.
  `install -D` needs the file as the final argument (`-D src dir/file`).
- `make clean` - removes objects, the binary and `build-kde/`.
- `make rpm` (Fedora) - `rpmbuild` from `3mfthumb.spec` via `spectool`
  (`rpm-build` + `spectool`). Builds `3mfthumb`, `3mfthumb-kde`, debuginfo and
  the source RPM. `Source0` is the GitHub **tag** archive, so the version must
  be pushed and tagged before the build can fetch it.
- `make deb` / `./make-deb.sh` - plain `dpkg-deb`, no `debhelper`/`dpkg-dev`.
  Builds both `.deb`s into the current directory; `3mfthumb-kde` is skipped
  when cmake/Qt6/KF6 KIO are missing. Version comes from the spec, revision
  from `debian/changelog` (override with `VERSION=`/`REVISION=`).
- `debian/` is also a full `debhelper` source package (two binary packages,
  `rules` stages files itself). Only the `make-deb.sh` path has been exercised
  (on Fedora).
- `./3mfthumb [-r] [-c] [-s N] [-x DEG] [-y DEG] [-z DEG] [-f RRGGBB]
  [-b RRGGBB]
  file.3mf|file.stl [out.png]` - see CLI contract.
- **KDE plugin**: `cmake -S kde-shellext -B build-kde && cmake --build build-kde`.
  Needs Qt 6, `KF6KIO` and ECM-less CMake (see gotchas). Installation paths are
  set in `kde-shellext/CMakeLists.txt`.
- **Windows cross-compile**: enter the mingw environment (`mingw64-env`) then
  `mingw64-make CFLAGS=-I./lib3mf_sdk/Bindings/Cpp LDFLAGS="-l3mf -lzip -lz -L./lib3mf_sdk/Lib"`.
  Needs `mingw64-gcc-g++`, `mingw64-zlib`, `mingw64-libzip` and the
  `lib3mf.dll` from the SDK zip.
- **Windows shell extension build**: cross-compile
  `win-shellext/ThumbnailProvider.cpp` with mingw-w64; see
  `win-shellext/README.md`. It uses the `CppDynamic` binding
  (`lib3mf_dynamic.hpp`) and links `-lshlwapi -lole32 -loleaut32 -luuid -lgdi32
  -lwindowscodecs`. Register with `regsvr32` (per-user, HKCU only).
- **CMake alternative (cross-platform only)**: the root `CMakeLists.txt` is a
  unified Linux/Windows build, tracked in git. the Makefile is
  the primary build. Cross-build: `cmake -S . -B build-win
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++`.

Packaging targets **1.2.2** (tag `v1.2.2`): bump `3mfthumb.spec` (`Version:`,
`Source0` follows `%{version}`), `debian/changelog` and the README download
links together for a new release.

There is **no test suite**. Verification is manual: run `./3mfthumb` on a
sample and inspect the output (`file out.png`, `identify -format '%[fx:mean]'`).

## CLI contract

`3mfthumb [-r] [-c] [-s N] [-x DEG] [-y DEG] [-z DEG] [-f RRGGBB] [-b RRGGBB]
<file.3mf|file.stl> [output.png]`

- Options come before the filenames; `--` ends them. Unknown options print an
  error and return `1`.
- Without an output path the image goes to the cache entry (`-c`) or, on
  Windows builds, where Explorer expects it; otherwise usage is printed and the
  exit code is `1`.
- `-r/--render` ignores an embedded preview and renders the meshes.
- `-c/--cache` additionally writes the freedesktop thumbnail cache entry:
  `<md5 of "file://<absolute path>">.png` under `$XDG_CACHE_HOME/thumbnails`
  (or `~/.cache/thumbnails`), in the `normal` (<=128), `large` (<=256) or
  `x-large` group selected by `-s`. The MD5 is bundled in `3mfthumb.cpp`
  (`md5Hex`); cache write failures are silent and the exit code stays `0`.
- `-s/--size N` - render edge length, default 256, clamped to 16..4096.
- `-x/--roll DEG` - rolls the view about its line of sight, any value; a
  missing value is an error.
- `-y/--tilt DEG` - camera elevation: 0 looks at the model head-on, 90 looks
  straight down. Values outside 0..90, and a missing value, print an error
  and return `1`.
- `-z/--spin DEG` - camera yaw about the vertical axis: 0 faces the model's
  front, 35 (the default) is the historical view from the right. Any value is
  accepted and a missing one is an error.
- All three are **absolute** aims, not offsets: `-x 0 -y 30 -z 35` renders
  exactly what the renderer always did, so output is unchanged unless a flag
  is passed. They ride in `render::ViewRotation` and only apply to rendered
  images, never to a copied embedded preview. The camera never rotates the
  model: it orbits it with world +Z kept upright (roll then spins the screen
  axes), so every aim frames the model the same way.
- `-f/--foreground RRGGBB` - colour for triangles that carry no material
  colour (all STL, most glTF); 3MF material colours still win. Default is the
  shaded grey-blue flat colour.
- `-b/--background RRGGBB` - composites rendered images onto an opaque
  background; default is transparent. Both colour flags accept `#rrggbb`,
  `#rgb` and an ignored trailing alpha, have no effect on embedded previews
  (copied verbatim), print an error for bad values and return `1`.
- `-h/--help` prints the usage text and returns `0`; missing arguments print
  the same text to stderr and return `1`.
- Input may be a `file://` URI (percent-escapes decoded, `file://localhost/`
  handled), because GNOME hands thumbnailers a URI inside its sandbox.
- STL input is selected by a case-insensitive `.stl` extension, bypasses
  Lib3MF entirely, and always renders (`-r` is implicit).
- `.glb` input also bypasses Lib3MF: a preview in the asset's `extras` wins
  (unless `-r`), otherwise the scene is rendered by `win-shellext/gltf.h`.
- Errors go to stderr prefixed with `3mfthumb: `.

**Gotcha:** exit code `0` does not imply an output file was written (a 3MF
without a package thumbnail and without `-c`/`-r` writes nothing). Only a
non-empty output file indicates success. A STL that fails to parse prints
"could not render" but also returns `0`.

## Architecture / control flow

`3mfthumb.cpp` (~380 lines) hands the shared renderer either a
`Lib3MF::PModel`, an STL path or a GLB path and then writes the RGBA buffer
out:

1. Parse options, decode `file://` URIs, lower-case the extension.
2. `.stl` -> `threemf::render::renderSTLToRGBA()` (parses binary STL, 84-byte
   header + count + 50-byte records, size-checked, else ASCII STL) ->
   `renderFacesToRGBA()`. Returns early; Lib3MF is never loaded.
3. `.glb` -> `renderGLBThumbnail()`: `threemf::gltf::embeddedThumbnail()`
   (extras `thumbnail`/`preview`/`thumb`, path or data URI) is copied out
   verbatim, else `threemf::gltf::renderGLBToRGBA()` (GLB chunks -> JSON
   members -> accessors -> node transforms -> `Face`s) ->
   `renderFacesToRGBA()`. Returns early; Lib3MF is never loaded.
4. Otherwise `Lib3MF::CWrapper::loadLibrary()` (the **implicit** API,
   `lib3mf_implicit.hpp`, resolves symbols at runtime), `CreateModel()`,
   `QueryReader("3mf")->ReadFromFile()`, then
   `HasPackageThumbnailAttachment()`/`GetPackageThumbnailAttachment()` and
   `WriteToFile()`.
5. No preview or `-r` -> `threemf::render::renderToRGBA()` ->
   `renderFacesToRGBA()`; `writePixels()` paints it into a Cairo surface on
   Linux or uses `writePNG()` on Windows.
6. All Lib3MF calls throw `std::exception`; caught and reported as exit `1`.

`renderer.h` structure: `Face`/`Vertex`, `collectFaces`/`collectObject` (3MF
build items), `readSTL`, `renderFacesToRGBA` (camera, painter's sort,
4x4 supersampled rasteriser; uncovered pixels stay transparent, so the PNG
has a transparent background and anti-aliased edges; `RenderColors` carries
optional foreground/background colours; `ViewRotation` is an absolute camera
orbit: elevation, yaw and roll),
`renderSTLToRGBA`,
`readModel`/`renderToRGBA`,
`writePNG`. Material colours from `basematerials`/`m:colorgroup` are used;
there are no textures, beam lattices or level sets. `Helix.3mf` from the SDK
exercises the renderer; real binary STLs (e.g. Blender exports) are good
end-to-end samples.

## Gotchas and non-obvious things

- **C++17 is required** (`<filesystem>`; an `#error` guards older standards).
  The Makefile now passes `-std=c++17`; keep `-O2`/`-g`.
- **`lib3mf_implicit.hpp` is not in the repo**; it comes from `lib3mf-devel`
  via pkg-config. clangd reports `pp_file_not_found` and cascades errors
  because it ignores pkg-config paths; those are expected. `kde-shellext/`
  diagnostics are also false positives (no `.clangd` there): it compiles
  through CMake's KIO include paths. `win-shellext/` has a scoped `.clangd`,
  so its diagnostics are real.
- **Arch Linux packaging bug**: `lib3mf` may install without symlinks to the C
  bindings; workaround `ln -s /usr/include/lib3mf/Bindings/Cpp/* /usr/include/lib3mf/`.
- **If pkg-config is missing**, the README documents substituting
  `BUILD_CFLAGS:=-I/usr/include/lib3mf` and `LDFLAGS:=-l3mf -lzip -lz`.
- **Windows argument count**: under `#ifdef WIN32` one argument is enough
  (`3mfthumb <file.3mf>` writes where Explorer expects it); on other builds a
  single argument is only accepted with `-c`.
- **`win-shellext/gltf.h` is shared by the CLI, the KDE plugin and the Windows shell extension**, so keep it dependency-free (it only includes `renderer.h`). It reads GLB by hand (chunk table plus targeted JSON scanning, no JSON library) and reuses `render::Affine`/`Face` so the 3MF and glTF paths end in the same rasterizer. Draco/meshopt-compressed primitives are skipped, not decoded.
- **The thumbnailer cache**: stale entries block new previews after an upgrade.
  The Makefile never wipes `/home/*/.cache/thumbnails` (root must not touch a
  user's cache); document `rm -rf $HOME/.cache/thumbnails/*`. A failed file
  leaves a marker under `~/.cache/thumbnails/fail/gnome-thumbnail-factory/`.
  File managers may need "previews for files > 1MB" enabled.
- **Never ship a Nautilus GResource overlay**: Nautilus only loads the `.so`
  extensions in `/usr/lib64/nautilus/extensions-4`; a stray
  `nautilus-thumbnailers.gresource` made it hang at startup. The old
  `install-nautilus` target is gone; the system-wide `/usr/share/thumbnailers`
  entry is the fix.
- **The GNOME thumbnail sandbox** (gnome-desktop's `bwrap` argv) mounts only
  `/usr`, `/etc/ld.so.cache`, `/proc`, `/dev` and a temp copy of the file,
  binds it as `/tmp/gnome-desktop-file-to-thumbnail.3mf`, and resolves
  `argv[0]` through the caller's `PATH`. No `.thumbnailer` key disables it.
  Reproduce with `bwrap --ro-bind /usr /usr ... --unshare-all 3mfthumb <file> /tmp/o.png`.
- **Nautilus thumbnails are unreliable to debug in-process**: GIO refuses to
  spawn thumbnailers without a registered application (`Failed to register:
  Timeout was reached`). Only a real desktop session is authoritative: wipe
  the cache, restart Nautilus, look for a `<md5 of URI>` file under
  `~/.cache/thumbnails/large/`.
- **Two thumbnailers for one MIME type is undefined.** Distros ship `stl-thumb`
  claiming `model/3mf`; with both registered Nautilus may pick the slow one, so
  remove that claim from the other thumbnailer.
- **Mime types**: `3mf.thumbnailer` and `kde-shellext/3mfthumbnail.json`
  advertise `model/3mf`, `application/vnd.ms-3mfdocument`, `model/stl` and
  `application/sla`. If a file manager ignores `.3mf`, suspect the MIME
  registration, not the binary.
- **KDE has no external-program thumbnailer**: KIO only loads shared libraries
  from the `thumbcreator` plugin directory (and KDE 6 no longer reads
  `.desktop` service files, hence only the `.json` is installed). The plugin,
  or a `-c` run from a custom action, is the only route. See the KDE plugin
  conventions below.
- **Do not commit build artifacts or the vendored SDK**: `.gitignore` covers
  `*.o`, `*.exe`, `*.dll`, `build-win/`, `build-kde/`, `*.deb`,
  `debian/3mfthumb*/`, `lib3mf_sdk/` and the unpacked `lib3mf_sdk_v*.zip`.
  `lib3mf_sdk/` is a manual download, not a submodule. The root
  `CMakeLists.txt` is still **not** tracked; `kde-shellext/CMakeLists.txt` is.
- **Windows shell extension is on `main`**: `win-shellext/` is an in-process
  `IThumbnailProvider` COM object (merged as PR #8), not a registry entry
  pointing at an exe. Don't regress the README to regedit advice.
- **KDE plugin conventions** (`kde-shellext/`, not compiled by the Makefile):
  `thumbcreator.cpp` is a `KIO::ThumbnailCreator` built with
  `K_PLUGIN_CLASS_WITH_JSON` and links Qt6 Core/Gui, `KF6::KIOGui` and lib3mf
  via pkg-config; `3mfthumbnail.json` declares `ThumbCreator` and
  `CacheThumbnail`. Fedora ships no KF6 umbrella CMake config, so the file uses
  `find_package(KF6KIO)`; it installs to `KDE_INSTALL_PLUGINDIR/thumbcreator`
  when ECM is present, else `${libdir}/qt6/plugins/thumbcreator`. It returns the
  package preview when Qt decodes it, otherwise renders. In the RPM it is the
  `3mfthumb-kde` subpackage gated by `%bcond_without kde` (check
  `rpmspec -P --without kde`: `rpmspec -q` still lists the subpackage); build
  it with plain `cmake`, not `%cmake`, whose build directory collides with
  `build-kde`. In `debian/` it is the second binary package; `make-deb.sh`
  derives its `Depends` from `ldd` minus the base binary's libraries. Verify by
  loading the `.so` with `QPluginLoader`, `KPluginFactory::create()`, then
  `create(KIO::ThumbnailRequest(url, size, mime, dpr, seq))` (PyQt6 here has no
  KIO bindings, so a small C++ harness is needed).
- **Releases**: tag `vX.Y.Z` is what `Source0` points at; push the tag before
  `make rpm`. Release assets are uploaded per package, keeping the base and
  KDE packages as separate files (no name collisions).
- **GPLv2** licensed; every source file carries the GPL header. Match the
  existing `///` comment style and keep attribution lines.

## Style / conventions

- One translation unit for the core tool, plus the self-contained
  `win-shellext/` translation unit and the CMake-built `kde-shellext/` plugin.
  Keep it simple.
- `///` doc-comment style in the source header.
- Commit history mixes conventional subjects with `Co-Authored-By:` trailers
  and PR merge commits. Author: Henry Kroll III
  (`nospam@thenerdshow.com`).
