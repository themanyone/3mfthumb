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

#include <iostream>
#ifdef WIN32
#include <filesystem>
#endif
#include "lib3mf_implicit.hpp"

int main(int argc, char **argv) {
  if (argc < 2){
    std::cout << "Usage: 3mfthumb <file.3mf> <output.png>" << std::endl;
    return 1;
  }
  // Get file names from command-line arguments
  std::string filename = argv[1];
  std::string thumbnailFilename = argv[2];
  try
  {
    auto wrapper = Lib3MF::CWrapper::loadLibrary();
    // Read the 3MF file
    Lib3MF::PModel model = wrapper->CreateModel();
    Lib3MF::PReader reader = model->QueryReader("3mf");
    reader->ReadFromFile(filename);
    // Get thumbnail attachment
    Lib3MF::PAttachment thumbnail;
    if (model->HasPackageThumbnailAttachment()) {
      thumbnail = model->GetPackageThumbnailAttachment();
      #ifdef WIN32
      if (argc == 2) {
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
    }
  }
  catch (std::exception &e)
  {
    std::cout << e.what() << std::endl;
    return 1;
  }
  
  return 0;
}
