/// 3mfthumb.cpp
///
/// Copyright 2023 Henry Kroll III <nospam@thenerdshow.com>
///
/// This program is free software; you can redistribute it and/or modify
/// it under the terms of the GNU General Public License as published by
/// the Free Software Foundation; either version 2 of the License, or
/// (at your option) any later version.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program; if not, write to the Free Software
/// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
/// MA 02110-1301, USA.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#if !defined(WIN32) && !defined(_WIN32)
#include <cairo.h>
#endif
#include "win-shellext/renderer.h"
#if __cplusplus >= 201703L // Check if C++17 or later is supported
#include <filesystem>
namespace fs = std::filesystem;
#else
#error "After git commit c22ea6b, C++17 or later is required for this code."
#endif


/// Write the shared renderer's RGBA buffer out as a PNG.
bool writePixels(const std::string &outputFilename, int size,
                 const std::vector<uint8_t> &pixels) {
#if !defined(WIN32) && !defined(_WIN32)
  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return false;
  }
  cairo_t *cr = cairo_create(surface);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const size_t i = (static_cast<size_t>(y) * size + x) * 4;
      cairo_set_source_rgb(cr, pixels[i] / 255.0, pixels[i + 1] / 255.0,
                           pixels[i + 2] / 255.0);
      cairo_rectangle(cr, x, y, 1, 1);
      cairo_fill(cr);
    }
  cairo_destroy(cr);
  cairo_status_t status =
      cairo_surface_write_to_png(surface, outputFilename.c_str());
  cairo_surface_destroy(surface);
  return status == CAIRO_STATUS_SUCCESS;
#else
  // No Cairo on Windows: the renderer writes the PNG itself.
  return threemf::render::writePNG(outputFilename.c_str(), size, pixels);
#endif
}

/// Paint the shared renderer's RGBA buffer into a cairo surface and save it.
bool renderThumbnail(Lib3MF::PModel model, const std::string &outputFilename,
                     int size) {
  std::vector<uint8_t> pixels;
  if (!threemf::render::renderToRGBA(model, size, pixels))
    return false;
  return writePixels(outputFilename, size, pixels);
}

/// STL files carry no embedded preview, so they always go through the
/// renderer.
bool renderSTLThumbnail(const std::string &filename,
                        const std::string &outputFilename, int size) {
  std::vector<uint8_t> pixels;
  if (!threemf::render::renderSTLToRGBA(filename, size, pixels))
    return false;
  return writePixels(outputFilename, size, pixels);
}

/// Lower-cased file extension, including the dot.
std::string extensionOf(const std::string &filename) {
  std::string extension = fs::path(filename).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return extension;
}

/// Size group that freedesktop thumbnail caches use for an edge length.
const char *groupForSize(int size) {
  if (size <= 128)
    return "normal";
  return size <= 256 ? "large" : "x-large";
}

/// MD5 of one string, as lowercase hex. Only used to name cache entries, so
/// it favours brevity over speed. Returns an empty string on failure.
std::string md5Hex(const std::string &text) {
  static const uint32_t k[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
      0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
      0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
      0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
      0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
      0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
      0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
      0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const int shifts[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7,
                                 12, 17, 22, 5, 9,  14, 20, 5,  9,  14, 20, 5,
                                 9, 14, 20, 5, 9,  14, 20, 4,  11, 16, 23, 4,
                                 11, 16, 23, 4, 11, 16, 23, 4,  11, 16, 23, 6,
                                 10, 15, 21, 6, 10, 15, 21, 6,  10, 15, 21, 6,
                                 10, 15, 21};
  std::vector<uint8_t> message(text.begin(), text.end());
  const uint64_t bitLength = static_cast<uint64_t>(message.size()) * 8;
  message.push_back(0x80);
  while (message.size() % 64 != 56)
    message.push_back(0);
  for (int i = 0; i < 8; ++i)
    message.push_back(static_cast<uint8_t>(bitLength >> (8 * i)));
  uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  for (size_t offset = 0; offset < message.size(); offset += 64) {
    uint32_t w[16];
    for (int i = 0; i < 16; ++i)
      w[i] = static_cast<uint32_t>(message[offset + i * 4]) |
             static_cast<uint32_t>(message[offset + i * 4 + 1]) << 8 |
             static_cast<uint32_t>(message[offset + i * 4 + 2]) << 16 |
             static_cast<uint32_t>(message[offset + i * 4 + 3]) << 24;
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; ++i) {
      uint32_t f, g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const uint32_t rotated = a + f + k[i] + w[g];
      a = d;
      d = c;
      c = b;
      b += (rotated << shifts[i]) | (rotated >> (32 - shifts[i]));
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
  }
  static const char digits[] = "0123456789abcdef";
  std::string hex;
  for (int i = 0; i < 4; ++i)
    for (int byte = 0; byte < 4; ++byte) {
      const uint8_t value = static_cast<uint8_t>(h[i] >> (8 * byte));
      hex += digits[value >> 4];
      hex += digits[value & 0xf];
    }
  return hex;
}

int main(int argc, char **argv) {
  bool forceRender = false;
  bool useCommonThumbnailCache = false;
  int size = 256;
  int argIndex = 1;
  for (; argIndex < argc && argv[argIndex][0] == '-' && argv[argIndex][1];
       ++argIndex) {
    const std::string option = argv[argIndex];
    if (option == "-r" || option == "--render") {
      forceRender = true;
    } else if (option == "-c" || option == "--cache") {
      useCommonThumbnailCache = true;
    } else if (option == "-s" || option == "--size") {
      if (argIndex + 1 < argc) {
        size = std::atoi(argv[++argIndex]);
        if (size < 16 || size > 4096)
          size = 256;
      }
    } else if (option == "--") {
      ++argIndex;
      break;
    } else {
      std::cerr << "3mfthumb: unknown option " << option << std::endl;
      return 1;
    }
  }
  // Without an output path the image goes to the shared cache (-c), or
  // wherever Windows' shell extension expects it.
#ifdef WIN32
  const bool needOutput = false;
#else
  const bool needOutput = !useCommonThumbnailCache;
#endif
  if (argc - argIndex < 1 || (argc - argIndex < 2 && needOutput)) {
    std::cout << "Usage: 3mfthumb [-r] [-c] [-s N] <file.3mf|file.stl>"
                 " [output.png]\n"
                 "  -r, --render  ignore the embedded preview and render the"
                 " meshes\n"
                 "  -c, --cache   also store the thumbnail in the shared"
                 " cache, as KDE expects\n"
                 "  -s, --size    render edge length in pixels (default 256,"
                 " maximum 4096)" << std::endl;
    return 1;
  }
  // Get file names from command-line arguments
  std::string filename = argv[argIndex];
  // Thumbnailer programs are handed a URI, not a plain path. Sandboxed
  // Nautilus may pass a localhost authority (file://localhost/path).
  static const std::string uriPrefixes[] = {"file://localhost/", "file://"};
  for (const std::string &uriPrefix : uriPrefixes) {
    if (filename.compare(0, uriPrefix.size(), uriPrefix) != 0)
      continue;
    filename.erase(0, uriPrefix.size());
    if (uriPrefix.back() == '/')
      filename.insert(filename.begin(), '/');
    std::string decoded;
    for (size_t i = 0; i < filename.size(); ++i) {
      if (filename[i] == '%' && i + 2 < filename.size() &&
          std::isxdigit(static_cast<unsigned char>(filename[i + 1])) &&
          std::isxdigit(static_cast<unsigned char>(filename[i + 2]))) {
        decoded += static_cast<char>(
            std::stoi(filename.substr(i + 1, 2), nullptr, 16));
        i += 2;
      } else {
        decoded += filename[i];
      }
    }
    filename = fs::path(decoded).lexically_normal().string();
    break;
  }
  std::string thumbnailFilename;
  if (argc > argIndex + 1)
    thumbnailFilename = argv[argIndex + 1];
  // KDE's thumbnail plugins store the image in the shared cache themselves
  // (CacheThumbnail=true), but those plugins are libraries. A thumbnailer
  // that runs us as a plain program has to write the cache entry here: the
  // file is named after the MD5 of the file's URI and lives under
  // $XDG_CACHE_HOME (or ~/.cache) in a size group.
  std::string cacheFilename;
  if (useCommonThumbnailCache) {
    const std::string uri =
        "file://" + fs::absolute(filename).lexically_normal().string();
    const char *cacheHome = std::getenv("XDG_CACHE_HOME");
    const char *home = std::getenv("HOME");
    fs::path directory;
    if (cacheHome && *cacheHome)
      directory = fs::path(cacheHome) / "thumbnails";
    else if (home && *home)
      directory = fs::path(home) / ".cache" / "thumbnails";
    if (!directory.empty()) {
      directory /= groupForSize(size);
      const std::string hash = md5Hex(uri);
      if (!hash.empty()) {
        std::error_code code;
        fs::create_directories(directory, code);
        if (!code)
          cacheFilename = (directory / (hash + ".png")).string();
      }
    }
  }
  // Without an output argument the cache entry is the only destination.
  std::string writeFilename = thumbnailFilename;
  if (writeFilename.empty())
    writeFilename = cacheFilename;
  // Copy the finished image into the cache, if there is one to copy.
  const auto copyToCache = [&]() {
    if (cacheFilename.empty() || cacheFilename == writeFilename)
      return;
    std::error_code code;
    if (fs::exists(writeFilename, code))
      fs::copy_file(writeFilename, cacheFilename,
                    fs::copy_options::overwrite_existing, code);
  };
  try
  {
    // STL has no embedded preview, so it never needs Lib3MF.
    if (extensionOf(filename) == ".stl") {
      if (writeFilename.empty())
        return 0;
      if (!renderSTLThumbnail(filename, writeFilename, size))
        std::cerr << "3mfthumb: could not render " << filename << std::endl;
      copyToCache();
      return 0;
    }
#if defined(_WIN32)
    // Windows: the dynamic binding wants an explicit DLL name, and
    // lib3mf.dll is installed next to the executable, which is also the
    // process's default search directory for LoadLibrary.
    auto wrapper = Lib3MF::CWrapper::loadLibrary("lib3mf.dll");
#else
    auto wrapper = Lib3MF::CWrapper::loadLibrary();
#endif
    // Read the 3MF file
    Lib3MF::PModel model = wrapper->CreateModel();
    Lib3MF::PReader reader = model->QueryReader("3mf");
    reader->ReadFromFile(filename);
    // Get thumbnail attachment
    Lib3MF::PAttachment thumbnail;
    if (!forceRender && model->HasPackageThumbnailAttachment()) {
      thumbnail = model->GetPackageThumbnailAttachment();
      #ifdef WIN32
      if (thumbnailFilename.empty()) {
        // Get a location Windows expects
        std::filesystem::path thumbnailPath = std::getenv("LOCALAPPDATA");
        thumbnailPath /= "Microsoft\\Windows\\Explorer\\" + thumbnail->GetRelationShipType();
        std::filesystem::create_directories(thumbnailPath);
        thumbnailPath /= std::filesystem::path(filename).stem().string() + ".thumbnail";
        thumbnailFilename = thumbnailPath.string();
      }
      #endif
      // Write thumbnail to file
      thumbnail->WriteToFile(thumbnailFilename);
      copyToCache();
    } else if (!writeFilename.empty()) {
      // No embedded preview (or -r): render the build meshes with Cairo.
      try {
        if (renderThumbnail(model, writeFilename, size))
          copyToCache();
      } catch (std::exception &e) {
        std::cerr << "3mfthumb: render failed: " << e.what() << std::endl;
      }
    }
  }
  catch (std::exception &e)
  {
    std::cerr << "3mfthumb: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
