# 3mfthumbail

Extract thumbnail images from 3MF files.

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

## Developer logic

The current version of the Lib3MF API uses the lib3mf_implicit.hpp header file instead of Lib3MF_Resources.hpp. This newer API provides a more simplified interface for working with the Lib3MF library.

This code uses the CWrapper::loadLibrary() method to load the Lib3MF library and create a new model. It then uses the QueryReader() method to create a new reader for the 3MF file format and read the model from a file using ReadFromFile(). Finally, we use the HasPackageThumbnailAttachment() and GetPackageThumbnailAttachment() methods to retrieve the thumbnail attachment and write it to a file using WriteToFile().

The `Makefile` uses `pkg-config --cflags --libs lib3mf` to obtain the compiler flags to use. If for some reason your system does not have pkg-config, you might be able to substitute these into the `Makefile`.
`-I/usr/include/lib3mf -l3mf -lzip -lz`

## Issues

We are new to C++ development, so there are bound to be some problems. 
Discuss issues on the [GitHub issue tracker](https://github.com/themanyone/3mfthumb/issues).

Copyright (C) 2023 Henry Kroll III, www.thenerdshow.com. See [LICENSE](LICENSE) for details.