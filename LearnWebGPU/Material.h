#pragma once
#include <webgpu/webgpu.hpp>

// Material describes GPU resources shared by many objects.
// (texture, sampler, lighting buffer, pipeline bindings)
class Material {
public:
  wgpu::Texture texture = nullptr;
  wgpu::TextureView textureView = nullptr;
  wgpu::Sampler sampler = nullptr;

  inline void destroy()
  {
    if (textureView) textureView.release();
    if (texture) {
      texture.destroy();
      texture.release();
    }
    if (sampler) sampler.release();
  }
};