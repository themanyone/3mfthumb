/// gltf.h
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

/// Reads binary glTF (.glb) as triangle soup for the shared renderer, and
/// pulls out a preview image if the asset carries one. No JSON library is
/// needed: the chunk table is walked by hand and the few members that hold
/// geometry are located by scanning, not by building a document tree.

#ifndef THREEMF_GLTF_H
#define THREEMF_GLTF_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "renderer.h"

namespace threemf {
namespace gltf {

/// A JSON value, represented by the span of text that holds it.
struct Span {
  const char *begin = nullptr;
  size_t length = 0;

  const char *end() const { return begin ? begin + length : nullptr; }
  bool empty() const { return begin == nullptr || length == 0; }
  std::string text() const {
    return empty() ? std::string() : std::string(begin, length);
  }
};

/// A forward-only cursor over JSON text.
struct Cursor {
  const char *p = nullptr;
  const char *end = nullptr;

  explicit Cursor(const Span &span)
      : p(span.begin), end(span.end()) {}
  Cursor(const char *begin, const char *stop) : p(begin), end(stop) {}

  void skipSpace() {
    while (p && p < end &&
           (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
      ++p;
  }

  bool at(char c) {
    skipSpace();
    return p && p < end && *p == c;
  }

  bool eat(char c) {
    if (!at(c))
      return false;
    ++p;
    return true;
  }

  /// Skips a string, including its escapes.
  void skipString() {
    if (!eat('"'))
      return;
    while (p < end && *p != '"') {
      if (*p == '\\')
        ++p;
      ++p;
    }
    if (p < end)
      ++p;
  }

  /// Copies a string, unescaping the simple escapes JSON allows.
  bool readString(std::string &out) {
    out.clear();
    if (!eat('"'))
      return false;
    while (p < end && *p != '"') {
      if (*p == '\\' && p + 1 < end) {
        ++p;
        switch (*p) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        default: out += *p; break;
        }
      } else {
        out += *p;
      }
      ++p;
    }
    return eat('"');
  }

  /// Skips one value of any type.
  bool skipValue() {
    skipSpace();
    if (!p || p >= end)
      return false;
    if (*p == '"') {
      skipString();
      return true;
    }
    if (*p != '{' && *p != '[') {
      while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
             *p != '\t' && *p != '\n' && *p != '\r')
        ++p;
      return true;
    }
    int depth = 0;
    while (p < end) {
      const char c = *p;
      if (c == '"') {
        skipString();
        continue;
      }
      if (c == '{' || c == '[')
        ++depth;
      else if (c == '}' || c == ']') {
        ++p;
        if (--depth == 0)
          return true;
        continue;
      }
      ++p;
    }
    return false;
  }

  /// Captures the value that starts here, leaving the cursor past it.
  bool value(Span &span) {
    skipSpace();
    const char *start = p;
    if (!skipValue())
      return false;
    span.begin = start;
    span.length = static_cast<size_t>(p - start);
    return true;
  }

  /// Positions at the first member value inside the object that starts here.
  bool openObject() {
    if (!eat('{'))
      return false;
    return !at('}');
  }

  /// Positions at the first element inside the array that starts here.
  bool openArray() {
    if (!eat('['))
      return false;
    return !at(']');
  }

  /// Steps over the separator between array elements and returns whether
  /// another element follows.
  bool nextElement() {
    skipSpace();
    if (p && p < end && *p == ']')
      return false;
    return eat(',') || eat('[');
  }
};

inline bool isArray(const Span &span) {
  return !span.empty() && *span.begin == '[';
}
inline bool isObject(const Span &span) {
  return !span.empty() && *span.begin == '{';
}

/// Finds a member of the object at ``object`` and returns its value.
inline bool member(const Span &object, const char *name, Span &out) {
  Cursor cursor{object.begin, object.end()};
  if (!cursor.openObject())
    return false;
  const size_t length = std::strlen(name);
  while (true) {
    cursor.skipSpace();
    if (cursor.p >= cursor.end || *cursor.p == '}')
      return false;
    if (cursor.p > object.begin && *cursor.p == ',')
      ++cursor.p;
    if (!cursor.eat('"'))
      return false;
    const char *keyBegin = cursor.p;
    int depth = 0;
    while (cursor.p < cursor.end) {
      if (*cursor.p == '\\') {
        ++cursor.p;
      } else if (*cursor.p == '"' && depth == 0) {
        break;
      }
      ++cursor.p;
    }
    if (cursor.p >= cursor.end)
      return false;
    const size_t keyLength = static_cast<size_t>(cursor.p - keyBegin);
    ++cursor.p;
    if (!cursor.eat(':'))
      return false;
    Span value;
    if (!cursor.value(value))
      return false;
    if (keyLength == length && std::strncmp(keyBegin, name, length) == 0) {
      out = value;
      return true;
    }
  }
}

/// Reads a number member, keeping ``fallback`` when it is absent or not a
/// number.
inline double number(const Span &object, const char *name, double fallback) {
  Span value;
  if (!member(object, name, value) || value.empty())
    return fallback;
  char *next = nullptr;
  const double parsed = std::strtod(value.begin, &next);
  return next == value.begin ? fallback : parsed;
}

inline unsigned index(const Span &object, const char *name,
                      unsigned fallback = 0) {
  return static_cast<unsigned>(number(object, name, fallback));
}

inline bool stringValue(const Span &object, const char *name,
                        std::string &out) {
  Span value;
  if (!member(object, name, value))
    return false;
  Cursor cursor(value);
  return cursor.readString(out);
}

/// Reads an array of numbers such as ``translation`` or ``matrix``.
inline bool numbers(const Span &object, const char *name, double *out,
                    int count) {
  Span array;
  if (!member(object, name, array) || !isArray(array))
    return false;
  Cursor cursor(array);
  int found = 0;
  Span element;
  for (; found < count && cursor.value(element); ++found) {
    out[found] = std::strtod(element.begin, nullptr);
    if (!cursor.nextElement())
      break;
  }
  return found == count;
}

inline bool decodeBase64(const std::string &uri, std::vector<uint8_t> &out) {
  const size_t comma = uri.find(',');
  if (uri.compare(0, 5, "data:") != 0 || comma == std::string::npos ||
      uri.find("base64") > comma)
    return false;
  int table[256];
  for (int &slot : table)
    slot = -1;
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; alphabet[i]; ++i)
    table[static_cast<unsigned char>(alphabet[i])] = i;
  out.clear();
  out.reserve((uri.size() - comma) * 3 / 4);
  uint32_t buffer = 0;
  int bits = 0;
  for (size_t i = comma + 1; i < uri.size(); ++i) {
    const int value = table[static_cast<unsigned char>(uri[i])];
    if (value < 0) {
      if (uri[i] == '=')
        break;
      continue;
    }
    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>(buffer >> bits));
    }
  }
  return !out.empty();
}

/// Reads a whole file named next to the asset.
inline bool readSibling(const std::string &asset, const std::string &uri,
                        std::vector<uint8_t> &out) {
  if (uri.empty() || uri.compare(0, 7, "http://") == 0 ||
      uri.compare(0, 8, "https://") == 0)
    return false;
  const size_t slash = asset.find_last_of("/\\");
  const std::string path =
      (slash == std::string::npos ? std::string() : asset.substr(0, slash + 1)) +
      uri;
  FILE *file = std::fopen(path.c_str(), "rb");
  if (!file)
    return false;
  out.clear();
  uint8_t chunk[65536];
  size_t got;
  while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
    out.insert(out.end(), chunk, chunk + got);
  std::fclose(file);
  return !out.empty();
}

/// The JSON description and the binary chunk of a GLB, which buffers index
/// from byte zero.
struct Glb {
  std::string json;
  const uint8_t *bin = nullptr;
  size_t binLength = 0;
  std::string asset;
  mutable std::vector<std::vector<uint8_t>> buffers;
  mutable std::vector<bool> loaded;

  /// The header is magic, version and total length; then a length, a type
  /// and a payload per chunk. Only the first JSON and BIN chunk are used.
  bool parse(const std::string &data, const std::string &filename) {
    asset = filename;
    if (data.size() < 12 || data.compare(0, 4, "glTF") != 0)
      return false;
    size_t offset = 12;
    while (offset + 8 <= data.size()) {
      uint32_t length = 0, type = 0;
      std::memcpy(&length, data.data() + offset, 4);
      std::memcpy(&type, data.data() + offset + 4, 4);
      const size_t start = offset + 8;
      if (length > data.size() - start)
        return false;
      if (type == 0x4E4F534AU && json.empty()) // 'JSON'
        json.assign(data, start, length);
      else if (type == 0x004E4942U && !bin) { // 'BIN\0'
        bin = reinterpret_cast<const uint8_t *>(data.data()) + start;
        binLength = length;
      }
      offset = start + length;
    }
    return !json.empty();
  }

  Span root() const {
    Span span;
    span.begin = json.data();
    span.length = json.size();
    return span;
  }

  /// Bytes of a buffer: the BIN chunk for the uri-less first buffer, a data
  /// URI for embedded buffers, and a sibling file otherwise.
  const std::vector<uint8_t> *buffer(unsigned wanted) const {
    if (buffers.size() != loaded.size())
      loaded.assign(buffers.size(), false);
    if (wanted < buffers.size() && loaded[wanted])
      return &buffers[wanted];
    Span list;
    if (!member(root(), "buffers", list) || !isArray(list))
      return nullptr;
    Cursor cursor(list);
    for (unsigned i = 0; cursor.nextElement(); ++i) {
      Span entry;
      if (!cursor.value(entry))
        return nullptr;
      if (i != wanted)
        continue;
      std::vector<uint8_t> bytes;
      std::string uri;
      if (!stringValue(entry, "uri", uri)) {
        if (!bin)
          return nullptr;
        bytes.assign(bin, bin + binLength);
      } else if (!decodeBase64(uri, bytes) &&
                 !readSibling(asset, uri, bytes)) {
        return nullptr;
      }
      if (buffers.size() <= wanted) {
        buffers.resize(wanted + 1);
        loaded.resize(wanted + 1, false);
      }
      buffers[wanted] = std::move(bytes);
      loaded[wanted] = true;
      return &buffers[wanted];
    }
    return nullptr;
  }
};

/// Reads an accessor into ``values`` (flattened, ``components`` per element).
/// Interleaved attributes are honoured through the buffer view's
/// ``byteStride``. Unsupported component types, sparse and compressed
/// accessors fail rather than render garbage.
inline bool readAccessor(const Glb &glb, unsigned wanted,
                         std::vector<double> &values, int &components) {
  Span list;
  if (!member(glb.root(), "accessors", list) || !isArray(list))
    return false;
  Cursor cursor(list);
  Span accessor;
  for (unsigned i = 0; cursor.nextElement(); ++i) {
    Span entry;
    if (!cursor.value(entry))
      return false;
    if (i == wanted) {
      accessor = entry;
      break;
    }
  }
  if (accessor.empty())
    return false;
  Span sparse;
  if (member(accessor, "sparse", sparse))
    return false;
  Span viewIndex;
  if (!member(accessor, "bufferView", viewIndex))
    return false;
  const unsigned componentType = index(accessor, "componentType");
  const size_t count = static_cast<size_t>(number(accessor, "count", 0));
  const size_t offset = static_cast<size_t>(number(accessor, "byteOffset", 0));
  std::string type;
  if (!stringValue(accessor, "type", type) || !count)
    return false;
  components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3
               : type == "VEC4" ? 4 : 0;
  if (!components)
    return false;

  Span views;
  if (!member(glb.root(), "bufferViews", views) || !isArray(views))
    return false;
  Cursor viewCursor(views);
  Span view;
  for (unsigned i = 0; viewCursor.nextElement(); ++i) {
    Span entry;
    if (!viewCursor.value(entry))
      return false;
    if (i == index(accessor, "bufferView")) {
      view = entry;
      break;
    }
  }
  if (view.empty())
    return false;
  const int componentSize = componentType == 5120 || componentType == 5121 ? 1
                            : componentType == 5122 || componentType == 5123 ? 2
                            : componentType == 5125 || componentType == 5126 ? 4
                                                                             : 0;
  if (!componentSize)
    return false;
  const size_t elementSize = static_cast<size_t>(componentSize) * components;
  const size_t stride = static_cast<size_t>(
      number(view, "byteStride", static_cast<double>(elementSize)));
  const std::vector<uint8_t> *bytes = glb.buffer(index(view, "buffer"));
  if (!bytes)
    return false;
  const size_t viewOffset = static_cast<size_t>(number(view, "byteOffset", 0));
  values.resize(count * components);
  for (size_t i = 0; i < count; ++i) {
    const size_t start = viewOffset + offset + i * stride;
    if (start + elementSize > bytes->size())
      return false;
    for (int c = 0; c < components; ++c) {
      const uint8_t *p = bytes->data() + start + c * componentSize;
      double v = 0;
      switch (componentType) {
      case 5120: { int8_t s; std::memcpy(&s, p, 1); v = s; break; }
      case 5121: v = *p; break;
      case 5122: { int16_t s; std::memcpy(&s, p, 2); v = s; break; }
      case 5123: { uint16_t s; std::memcpy(&s, p, 2); v = s; break; }
      case 5125: { uint32_t s; std::memcpy(&s, p, 4); v = s; break; }
      case 5126: { float s; std::memcpy(&s, p, 4); v = s; break; }
      }
      values[i * components + c] = v;
    }
  }
  return true;
}

/// glTF is Y-up while the renderer's other inputs are Z-up, so the preview
/// is rotated as a whole to stand the same way.
inline render::Affine yUpToZUp() {
  render::Affine m{};
  m.m[0][0] = 1;
  m.m[1][1] = 0;
  m.m[1][2] = -1;
  m.m[2][1] = 1;
  m.m[2][2] = 0;
  return m;
}

/// Local transform of a node: either ``matrix`` (column-major) or the TRS
/// properties, which compose into the same affine form.
inline render::Affine nodeTransform(const Span &node) {
  double matrix[16];
  Span span;
  render::Affine own;
  if (member(node, "matrix", span) && numbers(node, "matrix", matrix, 16)) {
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        own.m[r][c] = matrix[c * 4 + r];
    return own;
  }
  double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  numbers(node, "translation", t, 3);
  numbers(node, "rotation", q, 4);
  numbers(node, "scale", s, 3);
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  own.m[0][0] = s[0] * (1 - 2 * (y * y + z * z));
  own.m[0][1] = s[1] * 2 * (x * y - z * w);
  own.m[0][2] = s[2] * 2 * (x * z + y * w);
  own.m[1][0] = s[0] * 2 * (x * y + z * w);
  own.m[1][1] = s[1] * (1 - 2 * (x * x + z * z));
  own.m[1][2] = s[2] * 2 * (y * z - x * w);
  own.m[2][0] = s[0] * 2 * (x * z - y * w);
  own.m[2][1] = s[1] * 2 * (y * z + x * w);
  own.m[2][2] = s[2] * (1 - 2 * (x * x + y * y));
  own.m[0][3] = t[0];
  own.m[1][3] = t[1];
  own.m[2][3] = t[2];
  return own;
}

inline render::Vertex transform(const render::Affine &m, const double *v) {
  return render::Vertex{
      m.m[0][0] * v[0] + m.m[0][1] * v[1] + m.m[0][2] * v[2] + m.m[0][3],
      m.m[1][0] * v[0] + m.m[1][1] * v[1] + m.m[1][2] * v[2] + m.m[1][3],
      m.m[2][0] * v[0] + m.m[2][1] * v[1] + m.m[2][2] * v[2] + m.m[2][3]};
}

/// Base color of a primitive's material, in the renderer's 0..1 range, or
/// -1 to shade by normal.
inline void materialColor(const Glb &glb, const Span &primitive, double *color) {
  color[0] = color[1] = color[2] = -1;
  Span span;
  if (!member(primitive, "material", span))
    return;
  Span list;
  if (!member(glb.root(), "materials", list) || !isArray(list))
    return;
  Cursor cursor(list);
  for (unsigned i = 0; cursor.nextElement(); ++i) {
    Span entry;
    if (!cursor.value(entry))
      return;
    if (i != index(primitive, "material"))
      continue;
    Span pbr;
    if (member(entry, "pbrMetallicRoughness", pbr))
      numbers(pbr, "baseColorFactor", color, 3);
    return;
  }
}

/// One primitive's triangles in node space. Only triangle lists (mode 4)
/// carry surfaces; other topologies are skipped.
inline void readPrimitive(const Glb &glb, const Span &primitive,
                          const render::Affine &xform,
                          std::vector<render::Face> &faces, bool indexed) {
  Span attributes;
  Span positions;
  if (!member(primitive, "attributes", attributes) ||
      !member(attributes, "POSITION", positions))
    return;
  if (number(primitive, "mode", 4) != 4)
    return;
  std::vector<double> vertices, indices;
  int components = 0;
  if (!readAccessor(glb, static_cast<unsigned>(std::strtod(positions.begin, nullptr)),
                    vertices, components) ||
      components != 3)
    return;
  Span indexSpan;
  if (indexed && member(primitive, "indices", indexSpan)) {
    int ignored = 0;
    if (!readAccessor(glb, static_cast<unsigned>(std::strtod(indexSpan.begin, nullptr)),
                      indices, ignored))
      return;
  }
  const size_t vertexCount = vertices.size() / 3;
  const size_t orderCount = indexed ? indices.size() : vertexCount;
  double color[3];
  materialColor(glb, primitive, color);
  for (size_t i = 0; i + 3 <= orderCount; i += 3) {
    render::Face face;
    bool ok = true;
    for (int corner = 0; corner < 3; ++corner) {
      const size_t vertex =
          indexed ? static_cast<size_t>(indices[i + corner]) : i + corner;
      if (vertex >= vertexCount) {
        ok = false;
        break;
      }
      face.v[corner] = transform(xform, &vertices[vertex * 3]);
    }
    if (!ok)
      continue;
    face.r = color[0];
    face.g = color[1];
    face.b = color[2];
    faces.push_back(face);
  }
}

/// Renders a node (and its children) into ``faces``. ``parent`` is the world
/// transform above this node.
inline void readNode(const Glb &glb, unsigned wanted, const render::Affine &parent,
                     std::vector<render::Face> &faces, int depth) {
  if (depth > 32)
    return;
  Span list;
  if (!member(glb.root(), "nodes", list) || !isArray(list))
    return;
  Cursor cursor(list);
  for (unsigned i = 0; cursor.nextElement(); ++i) {
    Span node;
    if (!cursor.value(node))
      return;
    if (i != wanted)
      continue;
    const render::Affine world = render::multiply(parent, nodeTransform(node));

    Span meshes;
    Span meshSpan;
    if (member(glb.root(), "meshes", meshes) && isArray(meshes) &&
        member(node, "mesh", meshSpan)) {
      Cursor meshCursor(meshes);
      for (unsigned m = 0; meshCursor.nextElement(); ++m) {
        Span mesh;
        if (!meshCursor.value(mesh))
          break;
        if (m != index(node, "mesh"))
          continue;
        Span primitives;
        if (member(mesh, "primitives", primitives) && isArray(primitives)) {
          Cursor primitiveCursor(primitives);
          while (primitiveCursor.nextElement()) {
            Span primitive;
            if (!primitiveCursor.value(primitive))
              break;
            readPrimitive(glb, primitive, world, faces, true);
          }
        }
        break;
      }
    }

    Span children;
    if (member(node, "children", children) && isArray(children)) {
      Cursor childCursor(children);
      while (childCursor.nextElement()) {
        Span child;
        if (!childCursor.value(child))
          break;
        readNode(glb, static_cast<unsigned>(std::strtod(child.begin, nullptr)),
                 world, faces, depth + 1);
      }
    }
    return;
  }
}

/// Flattens the files' default scene into triangle soup. Assets without a
/// declared scene are treated as the library of meshes the spec allows, so
/// every node is visited.
inline bool readGLB(const std::string &filename, std::vector<render::Face> &faces) {
  FILE *file = std::fopen(filename.c_str(), "rb");
  if (!file)
    return false;
  std::string data;
  uint8_t chunk[65536];
  size_t got;
  while ((got = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
    data.append(reinterpret_cast<const char *>(chunk), got);
  std::fclose(file);

  Glb glb;
  if (!glb.parse(data, filename))
    return false;
  const Span root = glb.root();
  if (!isObject(root))
    return false;
  const render::Affine yUp = yUpToZUp();

  Span scenes, sceneNodes;
  if (member(root, "scenes", scenes) && isArray(scenes)) {
    Cursor cursor(scenes);
    for (unsigned i = 0; cursor.nextElement(); ++i) {
      Span scene;
      if (!cursor.value(scene))
        break;
      if (i != index(root, "scene"))
        continue;
if (member(scene, "nodes", sceneNodes) && isArray(sceneNodes)) {
        Cursor nodeCursor(sceneNodes);
        while (nodeCursor.nextElement()) {
          Span node;
          if (!nodeCursor.value(node))
            break;
        readNode(glb, static_cast<unsigned>(std::strtod(node.begin, nullptr)), yUp,
                   faces, 0);
        }
      }
      return !faces.empty();
    }
  }

  // Without a scene, nodes that have no parent are the roots.
  Span nodes;
  if (!member(root, "nodes", nodes) || !isArray(nodes))
    return false;
  std::vector<bool> child(0);
  {
    Cursor list(nodes);
    for (unsigned i = 0; list.nextElement(); ++i) {
      Span node;
      if (!list.value(node))
        break;
      if (child.size() <= i)
        child.resize(i + 1, false);
      Span children;
      if (member(node, "children", children) && isArray(children)) {
        Cursor kids(children);
        while (kids.nextElement()) {
          Span kid;
          if (!kids.value(kid))
            break;
          const size_t index = static_cast<size_t>(std::strtod(kid.begin, nullptr));
          if (child.size() <= index)
            child.resize(index + 1, false);
          child[index] = true;
        }
      }
    }
  }
  Cursor cursor(nodes);
  for (unsigned i = 0; cursor.nextElement(); ++i) {
    Span node;
    if (!cursor.value(node))
      break;
    if (i < child.size() && child[i])
      continue;
    readNode(glb, i, yUp, faces, 0);
  }
  return !faces.empty();
}

/// Renders a GLB file into an RGBA pixel buffer the same way renderToRGBA
/// draws a 3MF model.
inline bool renderGLBToRGBA(const std::string &filename, int size,
                            std::vector<uint8_t> &pixels,
                            const render::RenderColors &colors =
                                render::RenderColors(),
                            const render::ViewRotation &rotation =
                                render::ViewRotation()) {
  std::vector<render::Face> faces;
  if (!readGLB(filename, faces))
    return false;
  return render::renderFacesToRGBA(faces, size, pixels, colors, rotation);
}

/// glTF has no package thumbnail, but exporters commonly keep a preview in
/// the root ``extras`` (as ``thumbnail``, ``preview`` or ``thumb``, either a
/// FileNameString path or a data URI) and sometimes in ``thumbnails``.
/// Returns the image bytes when the asset carries one.
inline bool embeddedThumbnail(const std::string &filename,
                              const std::string &data,
                              std::vector<uint8_t> &out) {
  Glb glb;
  if (!glb.parse(data, filename))
    return false;
  Span extras;
  if (!member(glb.root(), "extras", extras))
    return false;
  static const char *const keys[] = {"thumbnail", "preview", "thumb"};
  Span value;
  std::string uri;
  for (const char *key : keys) {
    if (member(extras, key, value)) {
      Cursor cursor(value);
      if (cursor.readString(uri))
        break;
      if (isObject(value) && stringValue(value, "uri", uri))
        break;
    }
    uri.clear();
  }
  if (uri.empty() && member(extras, "thumbnails", value) && isArray(value)) {
    Cursor cursor(value);
    Span first;
    if (cursor.nextElement() && cursor.value(first)) {
      Cursor element(first);
      if (!element.readString(uri))
        stringValue(first, "uri", uri);
    }
  }
  if (uri.empty())
    return false;
  if (decodeBase64(uri, out))
    return true;
  return readSibling(filename, uri, out);
}

} // namespace gltf
} // namespace threemf

#endif
