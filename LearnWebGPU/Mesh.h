#pragma once
#include <webgpu/webgpu.hpp>

#include "ResourceManager.h"

// GPU mesh container: owns vertex buffer and vertex count.
class Mesh {
public:
  bool create(wgpu::Device device, wgpu::Queue queue, const std::vector<ResourceManager::VertexAttributes>& vertices);

  void destroy();

private:

  wgpu::Buffer vertexBuffer = nullptr;
  uint32_t vertexCount = 0;
};