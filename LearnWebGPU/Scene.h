#pragma once
#include <vector>
#include "Mesh.h"
#include "RenderObject.h"

// Scene holds meshes and objects separately.
// Multiple objects can reuse the same mesh.
class Scene {
public:

  Mesh* createMesh()
  {
    meshes.emplace_back();
    return &meshes.back();
  }

  RenderObject* createObject()
  {
    objects.emplace_back();
    return &objects.back();
  }

  void destroy()
  {
    for (auto& obj : objects) obj.destroy();
    for (auto& mesh : meshes) mesh.destroy();
  }

  std::vector<Mesh> meshes;
  std::vector<RenderObject> objects;
};