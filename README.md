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

Copy `3mfthumb.exe` along with `lib3mf.dll` to somewhere on the Windows machine. Maybe in C:\Program Files

In theory, we can use `regedit` to designate 3mfthumb.exe as the preferred handler to generate image previews for .3mf files. ChatGPT suggested something similar to the following. If it doesn't work, you can ask ChatGPT or Bing to fix it up.

1. Open the Registry Editor by typing "regedit" in the Windows search bar and selecting "Registry Editor".
2. Navigate to HKEY_CLASSES_ROOT\.3mf.
3. Right-click on something like "ThumbnailHandler" and select "Modify".
4. Set the value data to the full path of 3mfthumb.exe (e.g. "C:\Program Files\3mfthumb.exe") and click "OK".
5. Close the Registry Editor.

If it works, when you navigate to a folder containing a .3mf file, Windows should generate a thumbnail using 3mfthumb.exe.

## Known Issues

[Kubuntu thumbnailer commands work, but nautilus fails to genereate](https://duckduckgo.com/?q=Kubuntu+sandboxing+thumbnailers+with+bubblewrap) ...

Try (re)installing ffmpegthumbnailer. After that, the system might be configured to 
show thumbnails, since this topic has generated a lot of discussion. Then again.

`rm -rf $HOME/.cache/thumbnails/*`

Due to budget and time constraints, image previews have not been tested on Windows yet. But we're working on it.
Discuss issues on the [GitHub issue tracker](https://github.com/themanyone/3mfthumb/issues).

## Author's links

    - GitHub https://github.com/themanyone
    - YouTube https://www.youtube.com/themanyone
    - Mastodon https://mastodon.social/@themanyone
    - Linkedin https://www.linkedin.com/in/henry-kroll-iii-93860426/
    - [TheNerdShow.com](http://thenerdshow.com/)

Copyright (C) 2024-2025 Henry Kroll III, www.thenerdshow.com. See [LICENSE](LICENSE) for details.
