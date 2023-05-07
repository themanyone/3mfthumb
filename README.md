# 3mfthumbail

Extract thumbnail images from 3MF files and display them in file managers.

Preview images are great. As .3mf is supposed to be the universal 3D Printing standard, and adoption is expected to be rapid, we should be able to see embedded thumbnail image previews for them in file managers like `pcmanfm`, `nautilus`, or even Windows `explorer`.

![preview](preview.png)

## Get it from GitHub

```
git clone https://github.com/themanyone/3mfthumb.git
cd 3mfthumb
```

## Install dependencies

Fedora, Centos.
`dnf install libzip-devel lib3mf-devel zlib-devel`

Ubuntu, Debian.
`dpkg -i libzip-dev lib3mf-dev zlib1g-dev`

We will assume you have a build environment such as `gcc-c++` and `make`.


## Build

```
make
make install
```

Configure your file manager preferences to show previews for files > 1MB.

## Developer logic

The current version of the Lib3MF API uses the lib3mf_implicit.hpp header file instead of Lib3MF_Resources.hpp. This newer API provides a more simplified interface for working with the Lib3MF library.

This code uses the CWrapper::loadLibrary() method to load the Lib3MF library and create a new model. It then uses the QueryReader() method to create a new reader for the 3MF file format and read the model from a file using ReadFromFile(). Finally, we use the HasPackageThumbnailAttachment() and GetPackageThumbnailAttachment() methods to retrieve the thumbnail attachment and write it to a file using WriteToFile().

The `Makefile` uses `pkg-config --cflags --libs lib3mf` to obtain the compiler flags to use. If for some reason your system does not have pkg-config, you might be able to substitute these into the `Makefile`.
`-I/usr/include/lib3mf -l3mf -lzip -lz`

## Cross compiling to Windows

To cross compile to Windows, you must first install dependencies. Use 
the package manager to install `mingw64-gcc-g++`, `mingw64-zlib`, and 
`mingw64-libzip`. Now we need the `lib3mf.dll`. We got ours from https://github.com/3MFConsortium/lib3mf/releases like so.

```
wget https://github.com/3MFConsortium/lib3mf/releases/download/v2.2.0/lib3mf_sdk_v2.2.0.zip
unzip lib3mf_sdk_v2.2.0.zip
```

Now we can use `mingw64-make` to build the windows executable, `emfthumb.exe`. Set LIBS to the location of the extracted lib3mf/Lib dir, which should already contain a precompiled library `lib3mf.dll`.

```
LIBS=-L/home/k/Downloads/src/lib3mf/Lib mingw64-make
```

Now copy the resulting `3mfthumb.exe` along with `lib3mf.dll` to the Windows machine.

Thumbnailers can be configured on Windows using the registry editor, `regedit`.

Yes, you can set your thumbnail image extractor to be the default thumbnail image generator for .3mf files in Windows by creating a registry entry that associates your program with the .3mf file extension.

Here are the steps you can follow to create this registry entry:

1. Open the Windows Registry Editor by typing "regedit" in the Start menu search box and pressing Enter.

2. Navigate to the following registry key: HKEY_CLASSES_ROOT\.3mf

3. If there is no "Default" value in this key, create one by right-clicking on the right-hand pane, selecting "New > String Value", and naming the value "Default".

4. Set the value of the "Default" key to a unique name for your thumbnail generator, such as "3mfthumb.exe".

5. Navigate to the following registry key: `HKEY_CLASSES_ROOT\MyThumbnailGenerator\shell\open\command`

6. Create a new "String Value" named "Default" in the right-hand pane and set its value to the path to `3mfthumb.exe`, followed by the "%1" parameter to pass the filename of the .3mf file being opened. For example:

7. "C:\3mfthumb.exe" "%1"

8. Save and close the Registry Editor.

After making these changes, Windows should use `3mfthumb.exe` to generate thumbnails for `.3mf` files when they are viewed in Explorer or other file managers on Windows.

## Issues

We are new to C++ development, so there are bound to be some problems. Image previews have not been tested on Windows yet. But we're working on it.
Discuss issues on the [GitHub issue tracker](https://github.com/themanyone/3mfthumb/issues).

Copyright (C) 2023 Henry Kroll III, www.thenerdshow.com. See [LICENSE](LICENSE) for details.