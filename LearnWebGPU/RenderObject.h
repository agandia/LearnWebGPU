#pragma once
#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>
#include "Mesh.h"

// Per-object uniforms replicated from shader layout
struct ObjectUniforms
{
  glm::mat4 projectionMatrix;
  glm::mat4 viewMatrix;
  glm::mat4 modelMatrix;
  glm::vec4 color;
  float time;
  float _pad[3];
};

// Have the compiler check byte alignment
static_assert(sizeof(ObjectUniforms) % 16 == 0);

class RenderObject {
public:
  Mesh* mesh = nullptr;

  ObjectUniforms uniforms{};
  wgpu::Buffer uniformBuffer = nullptr;
  wgpu::BindGroup bindGroup = nullptr;

  bool createUniforms(wgpu::Device device)
  {
    wgpu::BufferDescriptor desc{};
    desc.size = sizeof(ObjectUniforms);
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    uniformBuffer = device.createBuffer(desc);
    return uniformBuffer != nullptr;
  }

  void destroy()
  {
    if (uniformBuffer) {
      uniformBuffer.destroy();
      uniformBuffer.release();
    }

    if (bindGroup) bindGroup.release();
  }
};