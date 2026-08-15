# 3mfthumb

Extract thumbnail images from 3MF files and display them in file managers.

Preview images are great. As .3mf is supposed to be the universal 3D Printing standard, and adoption is expected to be rapid, we should be able to see embedded thumbnail image previews for them in file managers like `pcmanfm`, `nautilus`, or even Windows `explorer`.

![preview](preview.png)

## Get it from GitHub

```
git clone https://github.com/themanyone/3mfthumb.git
cd 3mfthumb
```

## Linux dependencies

Fedora, Centos.
`dnf install libzip-devel lib3mf-devel zlib-ng-devel`

Ubuntu, Debian.
`apt install libzip-dev lib3mf-dev zlib1g-dev`

Arch
`pacman -S libzip lib3mf zlib1g`

We will assume a GNU build environment with tools like `gcc-c++`, `pkg-config` and `make`.

## Linux Build

```
make
make install
```

*Multi-user environments.* Please do not use `sudo make install` unless the system has 
multiple graphical desktop users. In that case, you may edit `Makefile` to change `DESTDIR` 
to `INSTALL_ROOT`. And manually remove old thumbnails for each user:

`rm -rf $HOME/.cache/thumbnails/*`

Previews typically work after this. If not, log out of the desktop session (or reboot).

Don't forget to configure file manager preferences to show previews for files > 1MB.

`make uninstall` to remove these components if no longer needed.

## Developer logic

The current version of the [Lib3MF](https://3mf.io/) API uses the `lib3mf_implicit.hpp` 
header file instead of Lib3MF_Resources.hpp. Available from the 
[3MF SDK](https://github.com/3MFConsortium/lib3mf/releases), this newer API provides a 
simplified interface for working with the Lib3MF library. If installed in a 
different place, you can use the `updatedb` and `locate` commands to find it.

This code employs the `CWrapper::loadLibrary()` method to load the Lib3MF library and create a 
new model. It then uses the `QueryReader()` method to create a new reader for the 3MF file 
format and read the model from a file using `ReadFromFile()`. Finally, we query the 
`HasPackageThumbnailAttachment()` and `GetPackageThumbnailAttachment()` methods to retrieve the thumbnail 
attachment and write it to a file using `WriteToFile()`.

## Build problems

The `Makefile` uses `pkg-config --cflags --libs lib3mf` to obtain the compiler flags to use. In particular, the location of `lib3mf_implicit.hpp`. If for some reason your system does not have pkg-config, you might be able to substitute these into the `Makefile`.

`BUILD_CFLAGS:=-I/usr/include/lib3mf`

`LDFLAGS:=-l3mf -lzip -lz`

On Arch, `lib3mf-dev` may not be properly installed. The build script forgot to create simlinks to the C bindings. It should be filed as a bug. A command such as th following might work around the issue.

`ln -s /usr/include/lib3mf/Bindings/Cpp/* /usr/include/lib3mf/`

## Using [lib3mf_sdk](https://github.com/3MFConsortium/lib3mf/)

If it stubbornly refuses to build using the distro-provided packages, install zlib and libzip, or make sure their development libraries are installed. Then get the SDK.

```
cd #3mfthumb src dir
wget https://github.com/3MFConsortium/lib3mf/releases/download/v2.4.1/lib3mf_sdk_v2.4.1.zip
unzip lib3mf_sdk*
```

Now we can build 3mfthumb

```
make CFLAGS=-I./lib3mf_sdk/Bindings/Cpp LDFLAGS="-l3mf -lzip -lz -L./lib3mf_sdk/Lib"
```

## Cross compiling to Windows

To build Windows executable on Linux system, Use 
the distro's package manager to install `mingw64-gcc-g++`, `mingw64-zlib`, and 
`mingw64-libzip`. Now we need the `lib3mf.dll`, available from the above SDK download.

Now use `mingw64-make` to build the windows executable, `3mfthumb.exe`. Set LIBS to the location of the extracted lib3mf/Lib dir, which should already contain a precompiled library `lib3mf.dll`.

```
mingw64-env #important!
mingw64-make CFLAGS=-I./lib3mf_sdk/Bindings/Cpp LDFLAGS="-l3mf -lzip -lz -L./lib3mf_sdk/Lib"
```

## Windows install

`3mfthumb.exe` itself is just a command-line converter (`3mfthumb file.3mf out.png`)
-- it's not something Windows Explorer can call on its own. Explorer only
generates thumbnails through a registered in-process COM object that
implements `IThumbnailProvider`; there's no "run this exe and read its
output" mechanism the way there is for `pcmanfm`/`nautilus` on Linux. (An
earlier version of this README suggested pointing a `ThumbnailHandler`
registry value straight at `3mfthumb.exe` -- that doesn't work, since that
registry slot expects a CLSID, not an executable path.)

The [`win-shellext/`](win-shellext/) directory has the real fix: a small
COM shell extension DLL that reuses the same lib3mf extraction logic and
hands Explorer a proper thumbnail bitmap. See
[`win-shellext/README.md`](win-shellext/README.md) for build and install
instructions. Summary:

```
# cross-compile from Linux with mingw-w64
sudo apt install g++-mingw-w64-x86-64-win32
cd win-shellext
x86_64-w64-mingw32-g++ -std=c++17 -Wall -O2 \
  -I../lib3mf_sdk/lib3mf_sdk/Bindings/CppDynamic \
  -c ThumbnailProvider.cpp -o ThumbnailProvider.o
x86_64-w64-mingw32-g++ -shared -o 3mfthumbprovider.dll \
  ThumbnailProvider.o ThumbnailProvider.def \
  -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -lwindowscodecs \
  -static-libgcc -static-libstdc++
```

Then on the Windows machine, copy `3mfthumbprovider.dll` and `lib3mf.dll`
(from `lib3mf_sdk/lib3mf_sdk/Bin/`) to wherever you want them to live, and
register:

```
regsvr32 3mfthumbprovider.dll
```

This registers under `HKEY_CURRENT_USER\Software\Classes`, so no
administrator rights are needed. `regsvr32 /u 3mfthumbprovider.dll` undoes
it. Verified working against real Windows Explorer (via
`IShellItemImageFactory::GetImage`, the same API Explorer itself uses).

## Known Issues

[Kubuntu thumbnailer commands work, but nautilus fails to genereate](https://duckduckgo.com/?q=Kubuntu+sandboxing+thumbnailers+with+bubblewrap) ...

Try (re)installing ffmpegthumbnailer. After that, the system might be configured to 
show thumbnails, since this topic has generated a lot of discussion. Then again.

`rm -rf $HOME/.cache/thumbnails/*`

Windows Explorer thumbnails now work via the COM shell extension in
[`win-shellext/`](win-shellext/) -- see the Windows install section above.

Discuss issues on the [GitHub issue tracker](https://github.com/themanyone/3mfthumb/issues).

## Author's links

    - GitHub https://github.com/themanyone
    - YouTube https://www.youtube.com/themanyone
    - Mastodon https://mastodon.social/@themanyone
    - Linkedin https://www.linkedin.com/in/henry-kroll-iii-93860426/
    - [TheNerdShow.com](http://thenerdshow.com/)

Copyright (C) 2024-2025 Henry Kroll III, www.thenerdshow.com. See [LICENSE](LICENSE) for details.

Additions adding full Windows support by Don Eisele, 2026, https://toysmakeuspowerful.com/
