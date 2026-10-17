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
#include <limits>
#include <vector>
#include <lib3mf_implicit.hpp>
#include <cairo.h>
#if __cplusplus >= 201703L // Check if C++17 or later is supported
#include <filesystem>
namespace fs = std::filesystem;
#else
#error "After git commit c22ea6b, C++17 or later is required for this code."
#endif

namespace {

struct Vertex {
  double x, y, z;
};

struct Face {
  Vertex v[3];
  double r, g, b;
};

/// Row-major affine transform. Lib3MF stores sTransform as 4x3 (the last
/// column is implicit), which does not map cleanly onto C arrays, so copy
/// it out once and compose in this form.
struct Affine {
  double m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};

Affine toAffine(const Lib3MF::sTransform &t) {
  Affine a;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 3; ++c)
      a.m[r][c] = t.m_Fields[r][c];
  return a;
}

Affine multiply(const Affine &lhs, const Affine &rhs) {
  Affine out;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) {
      double sum = 0;
      for (int k = 0; k < 4; ++k)
        sum += lhs.m[r][k] * rhs.m[k][c];
      out.m[r][c] = sum;
    }
  return out;
}

void setFaceColor(Face &face, const Lib3MF::sColor &color) {
  face.r = color.m_Red / 255.0;
  face.g = color.m_Green / 255.0;
  face.b = color.m_Blue / 255.0;
}

/// Flatten a build item into world-space triangle soup, composing component
/// transforms along the way.
void collectFaces(Lib3MF::PModel model, Lib3MF::PObject object,
                  const Affine &xform, bool hasParentColor,
                  const Lib3MF::sColor &parentColor, std::vector<Face> &faces,
                  int depth = 0) {
  if (!object || depth > 8)
    return;
  if (object->IsComponentsObject()) {
    auto components =
        std::dynamic_pointer_cast<Lib3MF::CComponentsObject>(object);
    for (Lib3MF_uint32 i = 0; i < components->GetComponentCount(); ++i) {
      auto component = components->GetComponent(i);
      auto resource = component->GetObjectResource();
      if (!resource)
        continue;
      Affine child = component->HasTransform()
                         ? toAffine(component->GetTransform())
                         : Affine{};
      collectFaces(model, resource, multiply(xform, child), hasParentColor,
                   parentColor, faces, depth + 1);
    }
    return;
  }
  if (!object->IsMeshObject())
    return;
  // Wrapper objects are not exposed with RTTI across dlopen boundaries, so
  // build the derived shared_ptr directly instead of dynamic_pointer_cast.
  Lib3MF::PMeshObject mesh =
      std::static_pointer_cast<Lib3MF::CMeshObject>(object);
  std::vector<Lib3MF::sPosition> vertices;
  std::vector<Lib3MF::sTriangle> triangles;
  std::vector<Lib3MF::sTriangleProperties> properties;
  try {
    mesh->GetVertices(vertices);
    mesh->GetTriangleIndices(triangles);
    mesh->GetAllTriangleProperties(properties);
  } catch (std::exception &) {
    return;
  }
  Lib3MF_uint32 objectResourceID = 0, objectPropertyID = 0;
  bool hasObjectColor =
      mesh->GetObjectLevelProperty(objectResourceID, objectPropertyID);
  std::vector<Lib3MF::sColor> triangleColor(triangles.size());
  std::vector<bool> triangleHasColor(triangles.size(), false);
  for (size_t i = 0; i < triangles.size(); ++i) {
    if (hasParentColor) {
      triangleColor[i] = parentColor;
      triangleHasColor[i] = true;
      continue;
    }
    Lib3MF_uint32 resourceID = 0, propertyID = 0;
    if (i < properties.size()) {
      resourceID = properties[i].m_ResourceID;
      propertyID = properties[i].m_PropertyIDs[0];
    }
    if (propertyID == 0 && hasObjectColor) {
      resourceID = objectResourceID;
      propertyID = objectPropertyID;
    }
    if (propertyID == 0)
      continue;
    try {
      if (Lib3MF::PColorGroup colors = model->GetColorGroupByID(resourceID)) {
        triangleColor[i] = colors->GetColor(propertyID);
        triangleHasColor[i] = true;
      } else if (Lib3MF::PBaseMaterialGroup materials =
                     model->GetBaseMaterialGroupByID(resourceID)) {
        triangleColor[i] = materials->GetDisplayColor(propertyID);
        triangleHasColor[i] = true;
      }
    } catch (std::exception &) {
    }
  }
  std::vector<Vertex> transformed(vertices.size());
  for (size_t i = 0; i < vertices.size(); ++i) {
    const float *p = vertices[i].m_Coordinates;
    double in[3] = {p[0], p[1], p[2]};
    Vertex &v = transformed[i];
    v.x = in[0] * xform.m[0][0] + in[1] * xform.m[1][0] + in[2] * xform.m[2][0] +
          xform.m[3][0];
    v.y = in[0] * xform.m[0][1] + in[1] * xform.m[1][1] + in[2] * xform.m[2][1] +
          xform.m[3][1];
    v.z = in[0] * xform.m[0][2] + in[1] * xform.m[1][2] + in[2] * xform.m[2][2] +
          xform.m[3][2];
  }
  for (size_t n = 0; n < triangles.size(); ++n) {
    const auto &t = triangles[n];
    Face f;
    bool valid = true;
    for (int i = 0; i < 3; ++i) {
      if (t.m_Indices[i] >= transformed.size()) {
        valid = false;
        break;
      }
      f.v[i] = transformed[t.m_Indices[i]];
    }
    if (valid) {
      f.r = f.g = f.b = -1; // unset: fall back to normal shading
      if (triangleHasColor[n])
        setFaceColor(f, triangleColor[n]);
      faces.push_back(f);
    }
  }
}

/// Lambert shade for a world-space triangle: surfaces facing the light
/// direction (unit vector, pointing towards the light) are brightest.
double shadeForNormal(const Vertex &a, const Vertex &b, const Vertex &c,
                      const double light[3]) {
  double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
  double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
  double nx = uy * vz - uz * vy;
  double ny = uz * vx - ux * vz;
  double nz = ux * vy - uy * vx;
  double len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len > 0) {
    nx /= len;
    ny /= len;
    nz /= len;
  }
  double lambert = std::max(0.0, nx * light[0] + ny * light[1] + nz * light[2]);
  return 0.18 + 0.82 * lambert;
}

/// Collect the triangles of one mesh object that ``objectID`` names. Build
/// items are skipped on purpose: slicers store a copy of the model for the
/// build section (already arranged on the plate) plus the originals as plain
/// resources, and drawing both renders every part twice.
void collectObject(Lib3MF::PModel model, Lib3MF_uint32 objectID,
                   std::vector<Face> &faces, int depth = 0) {
  if (depth > 8)
    return;
  Lib3MF::PObject object;
  {
    Lib3MF::PObjectIterator objects = model->GetObjects();
    while (objects->MoveNext()) {
      Lib3MF::PObject candidate = objects->GetCurrentObject();
      if (candidate && candidate->GetUniqueResourceID() == objectID) {
        object = candidate;
        break;
      }
    }
  }
  if (!object)
    return;
  if (object->IsComponentsObject()) {
    // RTTI is unreliable for these wrappers, so cast unconditionally after
    // the type check instead of via dynamic_pointer_cast.
    auto components = std::static_pointer_cast<Lib3MF::CComponentsObject>(object);
    if (!components)
      return;
    for (Lib3MF_uint32 i = 0; i < components->GetComponentCount(); ++i) {
      auto component = components->GetComponent(i);
      if (!component)
        continue;
      Affine child = component->HasTransform()
                         ? toAffine(component->GetTransform())
                         : Affine{};
      Lib3MF::PObject resource = component->GetObjectResource();
      if (!resource)
        continue;
      collectFaces(model, resource, child, false, Lib3MF::sColor{}, faces,
                   depth + 1);
    }
    return;
  }
  collectFaces(model, object, Affine{}, false, Lib3MF::sColor{}, faces);
}

struct ScreenVertex {
  double x, y;
};

bool renderThumbnail(Lib3MF::PModel model, const std::string &outputFilename) {
  std::vector<Face> faces;
  auto buildItems = model->GetBuildItems();
  while (buildItems->MoveNext()) {
    auto item = buildItems->GetCurrent();
    if (!item)
      continue;
    collectObject(model, item->GetObjectResourceID(), faces);
  }
  if (faces.empty())
    return false;

  double minCorner[3] = {std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::max()};
  double maxCorner[3] = {-minCorner[0], -minCorner[1], -minCorner[2]};
  for (const auto &f : faces)
    for (const auto &v : f.v) {
      const double p[3] = {v.x, v.y, v.z};
      for (int i = 0; i < 3; ++i) {
        minCorner[i] = std::min(minCorner[i], p[i]);
        maxCorner[i] = std::max(maxCorner[i], p[i]);
      }
    }
  double center[3], radius = 0;
  for (int i = 0; i < 3; ++i)
    center[i] = (minCorner[i] + maxCorner[i]) / 2;
  if (maxCorner[0] <= minCorner[0] && maxCorner[1] <= minCorner[1] &&
      maxCorner[2] <= minCorner[2])
    return false;
  for (const auto &f : faces)
    for (const auto &v : f.v) {
      double dx = v.x - center[0], dy = v.y - center[1], dz = v.z - center[2];
      radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }

  // Orbit camera: yaw 35 degrees to the right of +X, elevation 30 degrees
  // above the plate. The frame is: x across the screen, y up the screen and
  // z towards the viewer with the model in front (z < 0), the usual OpenGL
  // convention, so screen_x = focal * x / -z.
  const double yaw = 35.0 * M_PI / 180.0, pitch = 30.0 * M_PI / 180.0;
  const double cy = std::cos(yaw), sy = std::sin(yaw);
  const double cp = std::cos(pitch), sp = std::sin(pitch);
  const double right[3] = {-sy, cy, 0};
  const double up[3] = {-cy * sp, -sy * sp, cp};
  const double back[3] = {cy * cp, sy * cp, sp};
  // Light from the upper left of the camera frame, rotated into world space.
  const double light[3] = {-right[0] + up[0] + back[0],
                           -right[1] + up[1] + back[1],
                           -right[2] + up[2] + back[2]};
  const double lightLength =
      std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
  const double lightUnit[3] = {light[0] / lightLength, light[1] / lightLength,
                               light[2] / lightLength};

  // Place the camera so the bounding sphere fills most of the frame.
  const int size = 256;
  const double fov = 30.0 * M_PI / 180.0;
  const double focal = 0.5 * size / std::tan(fov / 2);
  // Far enough that the bounding sphere projects to about 85% of the frame.
  const double cameraDistance = radius / std::sin(fov / 2);
  const double cameraX = center[0] + back[0] * cameraDistance;
  const double cameraY = center[1] + back[1] * cameraDistance;
  const double cameraZ = center[2] + back[2] * cameraDistance;

  struct Projected {
    ScreenVertex v[3];
    const Face *face;
    double depth;
  };
  std::vector<Projected> projected;
  projected.reserve(faces.size());
  for (const auto &f : faces) {
    Projected p;
    p.face = &f;
    p.depth = 0;
    bool visible = true;
    for (int i = 0; i < 3; ++i) {
      const double in[3] = {f.v[i].x - cameraX, f.v[i].y - cameraY,
                            f.v[i].z - cameraZ};
      const double x = in[0] * right[0] + in[1] * right[1] + in[2] * right[2];
      const double y = in[0] * up[0] + in[1] * up[1] + in[2] * up[2];
      // Distance in front of the camera along the view axis.
      const double depth = -(in[0] * back[0] + in[1] * back[1] + in[2] * back[2]);
      if (depth <= 1e-6) {
        visible = false;
        break;
      }
      p.v[i].x = size / 2.0 + focal * x / depth;
      p.v[i].y = size / 2.0 - focal * y / depth;
      p.depth += depth / 3.0;
    }
    if (visible)
      projected.push_back(p);
  }
  // Painter's algorithm: farthest triangles first.
  std::stable_sort(projected.begin(), projected.end(),
                   [](const Projected &a, const Projected &b) {
                     return a.depth > b.depth;
                   });

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return false;
  }
  cairo_t *cr = cairo_create(surface);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);
  for (const auto &p : projected) {
    const Face &f = *p.face;
    double r = f.r, g = f.g, b = f.b;
    if (r < 0) {
      double shade = shadeForNormal(f.v[0], f.v[1], f.v[2], lightUnit);
      r = 0.62 * shade;
      g = 0.70 * shade;
      b = 0.82 * shade;
    }
    cairo_move_to(cr, p.v[0].x, p.v[0].y);
    cairo_line_to(cr, p.v[1].x, p.v[1].y);
    cairo_line_to(cr, p.v[2].x, p.v[2].y);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.55, 0.58, 0.64, 0.35);
    cairo_set_line_width(cr, 0.5);
    cairo_stroke(cr);
  }
  cairo_destroy(cr);
  cairo_status_t status =
      cairo_surface_write_to_png(surface, outputFilename.c_str());
  cairo_surface_destroy(surface);
  return status == CAIRO_STATUS_SUCCESS;
}

} // namespace

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
    auto wrapper = Lib3MF::CWrapper::loadLibrary();
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
