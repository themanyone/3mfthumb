/// renderer.h
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
///
/// Mesh renderer shared by the command-line tool and the Windows shell
/// extension. It draws the 3MF build items into a BGRA pixel buffer so the
/// caller can turn it into whatever its platform needs (a Cairo surface on
/// Linux, a DIB section for an HBITMAP on Windows). Everything here lives
/// in the threemf::render namespace so the two sources can also be built
/// together.

#ifndef THREEMF_RENDERER_H
#define THREEMF_RENDERER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>
#if defined(THREEMF_USE_DYNAMIC_BINDING) || defined(_WIN32)
// The shell extension loads lib3mf.dll by absolute path (see
// ThumbnailProvider.cpp), and the Windows CLI links the import library, so
// both use the dynamic binding there.
#include <lib3mf_dynamic.hpp>
#else
#include <lib3mf_implicit.hpp>
#endif

namespace threemf {
namespace render {

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

inline Affine toAffine(const Lib3MF::sTransform &t) {
  Affine a;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 3; ++c)
      a.m[r][c] = t.m_Fields[r][c];
  return a;
}

inline Affine multiply(const Affine &lhs, const Affine &rhs) {
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

inline void setFaceColor(Face &face, const Lib3MF::sColor &color) {
  face.r = color.m_Red / 255.0;
  face.g = color.m_Green / 255.0;
  face.b = color.m_Blue / 255.0;
}

/// Flatten a build item into world-space triangle soup, composing component
/// transforms along the way.
inline void collectFaces(Lib3MF::PModel model, Lib3MF::PObject object,
                         const Affine &xform, std::vector<Face> &faces,
                         int depth = 0) {
  if (!object || depth > 8)
    return;
  if (object->IsComponentsObject()) {
    // RTTI is unreliable for these wrappers (and absent in the dynamic
    // binding), so cast unconditionally after the type check.
    auto components =
        std::static_pointer_cast<Lib3MF::CComponentsObject>(object);
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
      collectFaces(model, resource, multiply(xform, child), faces, depth + 1);
    }
    return;
  }
  if (!object->IsMeshObject())
    return;
  auto mesh = std::static_pointer_cast<Lib3MF::CMeshObject>(object);
  if (!mesh)
    return;
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
inline double shadeForNormal(const Vertex &a, const Vertex &b, const Vertex &c,
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

struct ScreenVertex {
  double x, y;
};

struct Projected {
  ScreenVertex v[3];
  const Face *face;
  double depth;
};

/// Collect the triangles of one mesh object that ``objectID`` names. Build
/// items are skipped on purpose: slicers store a copy of the model for the
/// build section (already arranged on the plate) plus the originals as plain
/// resources, and drawing both renders every part twice.
inline void collectObject(Lib3MF::PModel model, Lib3MF_uint32 objectID,
                          std::vector<Face> &faces, int depth = 0) {
  if (depth > 8)
    return;
  Lib3MF::PObject object;
  try {
    Lib3MF::PObjectIterator objects = model->GetObjects();
    while (objects->MoveNext()) {
      Lib3MF::PObject candidate = objects->GetCurrentObject();
      if (candidate && candidate->GetUniqueResourceID() == objectID) {
        object = candidate;
        break;
      }
    }
  } catch (std::exception &) {
    return;
  }
  if (!object)
    return;
  collectFaces(model, object, Affine{}, faces);
}

/// Renders the model's build items into an RGBA pixel buffer (row-major,
/// top row first, alpha always 255) of ``size`` x ``size`` pixels, on a
/// white background. Returns false when the model has nothing to draw.
inline bool renderToRGBA(Lib3MF::PModel model, int size,
                         std::vector<uint8_t> &pixels) {
  if (size <= 0)
    return false;
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
  if (maxCorner[0] <= minCorner[0] && maxCorner[1] <= minCorner[1] &&
      maxCorner[2] <= minCorner[2])
    return false;
  double center[3], radius = 0;
  for (int i = 0; i < 3; ++i)
    center[i] = (minCorner[i] + maxCorner[i]) / 2;
  for (const auto &f : faces)
    for (const auto &v : f.v) {
      double dx = v.x - center[0], dy = v.y - center[1], dz = v.z - center[2];
      radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }

  // Orbit camera: yaw 35 degrees to the right of +X, elevation 30 degrees
  // above the plate. "back" points from the model towards the camera;
  // "right" and "up" are the screen axes.
  const double pi = 3.14159265358979323846;
  const double yaw = 35.0 * pi / 180.0, pitch = 30.0 * pi / 180.0;
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

  // Far enough that the bounding sphere projects to about 85% of the frame.
  const double fov = 30.0 * pi / 180.0;
  const double focal = 0.5 * size / std::tan(fov / 2);
  const double cameraDistance = radius / std::sin(fov / 2);
  const double cameraX = center[0] + back[0] * cameraDistance;
  const double cameraY = center[1] + back[1] * cameraDistance;
  const double cameraZ = center[2] + back[2] * cameraDistance;

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

  const size_t stride = static_cast<size_t>(size) * 4;
  std::vector<double> buffer(static_cast<size_t>(size) * size * 3, 255.0);

  // Rasterize one screen-space triangle with 4x4 supersampled coverage.
  const int subSamples = 4;
  for (const auto &p : projected) {
    const Face &f = *p.face;
    double r = f.r, g = f.g, b = f.b;
    if (r < 0) {
      const double shade = shadeForNormal(f.v[0], f.v[1], f.v[2], lightUnit);
      r = 0.62 * shade;
      g = 0.70 * shade;
      b = 0.82 * shade;
    }
    double minX = std::min({p.v[0].x, p.v[1].x, p.v[2].x});
    double maxX = std::max({p.v[0].x, p.v[1].x, p.v[2].x});
    double minY = std::min({p.v[0].y, p.v[1].y, p.v[2].y});
    double maxY = std::max({p.v[0].y, p.v[1].y, p.v[2].y});
    int x0 = static_cast<int>(std::floor(minX));
    int x1 = static_cast<int>(std::ceil(maxX));
    int y0 = static_cast<int>(std::floor(minY));
    int y1 = static_cast<int>(std::ceil(maxY));
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, size - 1);
    y1 = std::min(y1, size - 1);
    const double area = (p.v[1].x - p.v[0].x) * (p.v[2].y - p.v[0].y) -
                        (p.v[2].x - p.v[0].x) * (p.v[1].y - p.v[0].y);
    if (std::fabs(area) < 1e-12)
      continue;
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        int covered = 0;
        for (int sy = 0; sy < subSamples; ++sy) {
          for (int sx = 0; sx < subSamples; ++sx) {
            const double px = x + (sx + 0.5) / subSamples;
            const double py = y + (sy + 0.5) / subSamples;
            const double w0 = ((p.v[1].x - px) * (p.v[2].y - py) -
                               (p.v[2].x - px) * (p.v[1].y - py)) / area;
            const double w1 = ((p.v[2].x - px) * (p.v[0].y - py) -
                               (p.v[0].x - px) * (p.v[2].y - py)) / area;
            const double w2 = 1.0 - w0 - w1;
            if (w0 >= 0 && w1 >= 0 && w2 >= 0)
              ++covered;
          }
        }
        if (!covered)
          continue;
        const double alpha = static_cast<double>(covered) / (subSamples * subSamples);
        const size_t offset = static_cast<size_t>(y) * static_cast<size_t>(size) * 3 +
                              static_cast<size_t>(x) * 3;
        buffer[offset] = buffer[offset] * (1 - alpha) + r * 255.0 * alpha;
        buffer[offset + 1] = buffer[offset + 1] * (1 - alpha) + g * 255.0 * alpha;
        buffer[offset + 2] = buffer[offset + 2] * (1 - alpha) + b * 255.0 * alpha;
      }
    }
  }

  pixels.assign(static_cast<size_t>(size) * size * 4, 255);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const size_t src = (static_cast<size_t>(y) * size + x) * 3;
      const size_t dst = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
      pixels[dst] = static_cast<uint8_t>(std::min(255.0, std::max(0.0, buffer[src])));
      pixels[dst + 1] =
          static_cast<uint8_t>(std::min(255.0, std::max(0.0, buffer[src + 1])));
      pixels[dst + 2] =
          static_cast<uint8_t>(std::min(255.0, std::max(0.0, buffer[src + 2])));
      pixels[dst + 3] = 255;
    }
  }
  return true;
}

/// Encodes an RGBA buffer made by renderToRGBA as a PNG file. Kept here so
/// the Windows shell extension can save rendered previews without pulling
/// in a separate image library.
inline bool writePNG(const char *filename, int size,
                     const std::vector<uint8_t> &rgba) {
  static const uint32_t crcTable[256] = {
      0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
      0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
      0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
      0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
      0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
      0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
      0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
      0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
      0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
      0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
      0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
      0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
      0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
      0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
      0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
      0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
      0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
      0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
      0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
      0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
      0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
      0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
      0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
      0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
      0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
      0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
      0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
      0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
      0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
      0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
      0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
      0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
      0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
      0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
      0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
      0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
      0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
      0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
      0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
      0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
      0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
      0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
      0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};
  struct Writer {
    std::vector<uint8_t> out;
    void byte(uint8_t v) { out.push_back(v); }
    void be32(uint32_t v) {
      out.push_back(static_cast<uint8_t>(v >> 24));
      out.push_back(static_cast<uint8_t>(v >> 16));
      out.push_back(static_cast<uint8_t>(v >> 8));
      out.push_back(static_cast<uint8_t>(v));
    }
    void chunk(const char *type, const std::vector<uint8_t> &data) {
      be32(static_cast<uint32_t>(data.size()));
      size_t start = out.size();
      out.insert(out.end(), type, type + 4);
      out.insert(out.end(), data.begin(), data.end());
      uint32_t crc = 0xffffffffu;
      for (size_t i = start; i < out.size(); ++i)
        crc = crcTable[(crc ^ out[i]) & 0xff] ^ (crc >> 8);
      be32(crc ^ 0xffffffffu);
    }
  } w;
  static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  w.out.insert(w.out.end(), signature, signature + 8);
  std::vector<uint8_t> ihdr;
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(static_cast<uint8_t>(size >> 8));
  ihdr.push_back(static_cast<uint8_t>(size));
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(static_cast<uint8_t>(size >> 8));
  ihdr.push_back(static_cast<uint8_t>(size));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(6);  // RGBA
  ihdr.push_back(0);  // deflate
  ihdr.push_back(0);  // adaptive filtering
  ihdr.push_back(0);  // no interlace
  w.chunk("IHDR", ihdr);
  // zlib stream: a stored (uncompressed) deflate block per 65535 bytes.
  std::vector<uint8_t> idat;
  idat.push_back(0x78);
  idat.push_back(0x01);
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(size) * (size * 4 + 1));
  for (int y = 0; y < size; ++y) {
    raw.push_back(0); // filter type: none
    const uint8_t *row = &rgba[static_cast<size_t>(y) * size * 4];
    raw.insert(raw.end(), row, row + static_cast<size_t>(size) * 4);
  }
  size_t offset = 0;
  while (offset < raw.size()) {
    size_t block = std::min<size_t>(65535, raw.size() - offset);
    bool last = offset + block == raw.size();
    idat.push_back(last ? 1 : 0);
    idat.push_back(static_cast<uint8_t>(block));
    idat.push_back(static_cast<uint8_t>(block >> 8));
    idat.push_back(static_cast<uint8_t>(~block));
    idat.push_back(static_cast<uint8_t>(~block >> 8));
    idat.insert(idat.end(), raw.begin() + offset, raw.begin() + offset + block);
    offset += block;
  }
  // Adler-32 trailer of the zlib stream.
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  const uint32_t adler = (b << 16) | a;
  idat.push_back(static_cast<uint8_t>(adler >> 24));
  idat.push_back(static_cast<uint8_t>(adler >> 16));
  idat.push_back(static_cast<uint8_t>(adler >> 8));
  idat.push_back(static_cast<uint8_t>(adler));
  w.chunk("IDAT", idat);
  std::vector<uint8_t> empty;
  w.chunk("IEND", empty);

  FILE *file = fopen(filename, "wb");
  if (!file)
    return false;
  bool ok = fwrite(w.out.data(), 1, w.out.size(), file) == w.out.size();
  fclose(file);
  return ok;
}

} // namespace render
} // namespace threemf

#endif
