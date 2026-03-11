#include "Mesh.h"

#include <vector>

bool Mesh::create(wgpu::Device device, wgpu::Queue queue, const std::vector<ResourceManager::VertexAttributes>& vertices) {
  vertexCount = (uint32_t)vertices.size();

  wgpu::BufferDescriptor desc{};
  desc.size = vertices.size() * sizeof(ResourceManager::VertexAttributes);
  desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

  vertexBuffer = device.createBuffer(desc);
  queue.writeBuffer(vertexBuffer, 0, vertices.data(), desc.size);
  return vertexBuffer != nullptr;
}

void Mesh::destroy() {
  if (vertexBuffer) {
    vertexBuffer.destroy();
    vertexBuffer.release();
  }
  vertexCount = 0;
}