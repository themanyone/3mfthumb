/// thumbcreator.cpp
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
/// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
/// MA 02110-1301, USA.
///
/// Image formats come from Qt's PNG/JPEG plugins, which KIO loads for the
/// thumbnail cache; without them only the embedded formats Qt understands
/// natively survive.

#include "thumbcreator.h"

#include <KPluginFactory>

#include <QBuffer>
#include <QFile>
#include <QPixmap>

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <vector>

#include "win-shellext/gltf.h"
#include "win-shellext/renderer.h"

K_PLUGIN_CLASS_WITH_JSON(ThumbnailCreator, "3mfthumbnail.json")

namespace {

/// True when the embedded preview carries a format Qt can hand back, which
/// is cheaper and more faithful than re-rendering the meshes.
bool decodePreview(const Lib3MF::PAttachment &attachment, QImage &image) {
  std::vector<Lib3MF_uint8> bytes;
  try {
    attachment->ReadFromBuffer(bytes);
  } catch (std::exception &) {
    return false;
  }
  if (bytes.empty())
    return false;
  return image.loadFromData(QByteArray(reinterpret_cast<const char *>(bytes.data()),
                                       static_cast<int>(bytes.size())));
}

/// Binary glTF: the asset's own preview when it carries one, otherwise the
/// same renderer as the other formats.
bool renderGLB(const std::string &filename, int size, QImage &image) {
  QFile file(QString::fromStdString(filename));
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const QByteArray data = file.readAll();
  std::vector<uint8_t> bytes;
  if (threemf::gltf::embeddedThumbnail(filename,
                                       std::string(data.constData(), data.size()),
                                       bytes)) {
    image.loadFromData(reinterpret_cast<const uchar *>(bytes.data()),
                       static_cast<int>(bytes.size()));
    return !image.isNull();
  }
  std::vector<uint8_t> pixels;
  if (!threemf::gltf::renderGLBToRGBA(filename, size, pixels))
    return false;
  image = QImage(reinterpret_cast<const uchar *>(pixels.data()), size, size,
                 size * 4, QImage::Format_RGBA8888)
              .copy();
  return !image.isNull();
}

/// Renders the build items (3MF) or the triangles (STL) at ``size`` pixels.
bool renderMesh(const std::string &filename, bool isSTL, int size,
                QImage &image) {
  std::vector<uint8_t> pixels;
  try {
    if (isSTL) {
      if (!threemf::render::renderSTLToRGBA(filename, size, pixels))
        return false;
    } else {
      Lib3MF::PWrapper wrapper = Lib3MF::CWrapper::loadLibrary();
      Lib3MF::PModel model = wrapper->CreateModel();
      model->QueryReader("3mf")->ReadFromFile(filename);
      if (!threemf::render::renderToRGBA(model, size, pixels))
        return false;
    }
  } catch (std::exception &) {
    return false;
  }
  image = QImage(reinterpret_cast<const uchar *>(pixels.data()), size, size,
                 size * 4, QImage::Format_RGBA8888)
              .copy();
  return !image.isNull();
}

} // namespace

ThumbnailCreator::ThumbnailCreator(QObject *parent, const QVariantList &args)
    : KIO::ThumbnailCreator(parent, args) {}

KIO::ThumbnailResult ThumbnailCreator::create(
    const KIO::ThumbnailRequest &request) {
  const QString path = request.url().toLocalFile();
  if (path.isEmpty())
    return KIO::ThumbnailResult::fail();
  const QSize target =
      request.targetSize() * request.devicePixelRatio();

  std::string extension = path.section('.', -1).toLower().toStdString();
  const bool isSTL = extension == "stl";
  const bool is3MF = extension == "3mf" || extension == "3mfzip";
  const bool isGLB = extension == "glb";

  QImage image;
  if (is3MF) {
    try {
      Lib3MF::PWrapper wrapper = Lib3MF::CWrapper::loadLibrary();
      Lib3MF::PModel model = wrapper->CreateModel();
      model->QueryReader("3mf")->ReadFromFile(path.toStdString());
      if (model->HasPackageThumbnailAttachment())
        decodePreview(model->GetPackageThumbnailAttachment(), image);
    } catch (std::exception &) {
      // Fall through to the renderer, which reports its own failures.
    }
    if (image.isNull())
      renderMesh(path.toStdString(), false, target.width(), image);
  } else if (isSTL) {
    renderMesh(path.toStdString(), true, target.width(), image);
  } else if (isGLB) {
    renderGLB(path.toStdString(), target.width(), image);
  } else {
    return KIO::ThumbnailResult::fail();
  }
  if (image.isNull())
    return KIO::ThumbnailResult::fail();
  if (image.size() != target)
    image = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  return KIO::ThumbnailResult::pass(image);
}

#include "thumbcreator.moc"
