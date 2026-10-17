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


/// Paint the shared renderer's RGBA buffer into a cairo surface and save it.
bool renderThumbnail(Lib3MF::PModel model, const std::string &outputFilename) {
  const int size = 256;
#if !defined(WIN32) && !defined(_WIN32)
  std::vector<uint8_t> pixels;
  if (!threemf::render::renderToRGBA(model, size, pixels))
    return false;

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
  std::vector<uint8_t> pixels;
  if (!threemf::render::renderToRGBA(model, size, pixels))
    return false;
  return threemf::render::writePNG(outputFilename.c_str(), size, pixels);
#endif
}

int main(int argc, char **argv) {
  bool forceRender = false;
  int argIndex = 1;
  if (argc > argIndex &&
      (std::string(argv[argIndex]) == "-r" ||
       std::string(argv[argIndex]) == "--render")) {
    forceRender = true;
    ++argIndex;
  }
#ifdef WIN32
  if (argc - argIndex < 1) {
#else
  if (argc - argIndex < 2) {
#endif
    std::cout << "Usage: 3mfthumb [-r] <file.3mf> <output.png>\n"
                 "  -r, --render  ignore the embedded preview and render the"
                 " meshes" << std::endl;
    return 1;
  }
  // Get file names from command-line arguments
  std::string filename = argv[argIndex];
  // Thumbnailer programs are handed a URI, not a plain path.
  if (filename.compare(0, 7, "file://") == 0) {
    filename.erase(0, 7);
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
  }
  std::string thumbnailFilename;
  if (argc > argIndex + 1)
    thumbnailFilename = argv[argIndex + 1];
  try
  {
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
    } else if (!thumbnailFilename.empty()) {
      // No embedded preview (or -r): render the build meshes with Cairo.
      try {
        renderThumbnail(model, thumbnailFilename);
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
