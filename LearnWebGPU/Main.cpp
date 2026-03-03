/**
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://github.com/eliemichel/LearnWebGPU
 *
 * MIT License
 * Copyright (c) 2022-2024 Elie Michel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

 /**
 * Although based on the licensed implementation above, this file contains modifications
 * that are not part of the original work. These modifications are a result of a learning
 * exercise on my end.
 */

 // Replaced default <webgpu/webgpu.hpp> with the webgpu.hpp version,
 // containing some syntactic sugar and C++ wrappers
#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu/webgpu.hpp"

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp> // all types inspired from GLSL
#include <glm/ext.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif // __EMSCRIPTEN__

#include <iostream>
#include <cassert>
//#include <array>
#include <vector>

#include "ResourceManager.h"

using namespace wgpu;

constexpr float PI = 3.14159265358979323846f;

class Application {
public:
  // Initialize all or catch errors.
  bool Initialize();

  // Clean everything
  void Terminate();

  // Draw a frame and handle events
  void MainLoop();

  // Main loop keepalive check.
  bool IsRunning();

private:

  /**
   * The same structure as in the shader, replicated in C++
   */
  struct MyUniforms {
    // We add transform matrices
    glm::mat4 projectionMatrix;
    glm::mat4 viewMatrix;
    glm::mat4 modelMatrix;
    glm::vec4 color;
    float time;
    float _pad[3];
  } uniforms;

  /**
 * A structure that describes the data layout in the vertex buffer
 * We do not instantiate it but use it in `sizeof` and `offsetof`
 */
  struct VertexAttributes {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
  };

  // Have the compiler check byte alignment
  static_assert(sizeof(MyUniforms) % 16 == 0);

  TextureView GetNextSurfaceTextureView();
  void GetDepthTextureResources();

  void InitializePipeline();
  RequiredLimits GetRequiredLimits(Adapter adapter) const;
  void InitializeBuffers();
  void InitializeBindGroups();

  // Shared data between init and main loop.
  GLFWwindow* window;
  Device device;
  Queue queue;
  Surface surface;
  TextureFormat surfaceFormat = TextureFormat::Undefined;
  TextureFormat depthTextureFormat = TextureFormat::Depth24Plus;
  Texture depthTexture;
  TextureView depthTextureView;
  RenderPipeline pipeline;
  Buffer pointBuffer;
  Buffer indexBuffer;
  Buffer uniformBuffer;
  uint32_t indexCount;
  BindGroup bindGroup;
  PipelineLayout layout;
  BindGroupLayout bindGroupLayout;

  std::unique_ptr<ErrorCallback> uncapturedErrorCallbackHandle;
};

int main(int, char**)
{
  Application app;

  if (!app.Initialize()) {
    std::cerr << "Failed to initialize the application. Program terminated" << std::endl;
    return 1;
  }

#ifdef __EMSCRIPTEN__
  // In the Emscripten backend, we use emscripten_set_main_loop to call the main loop function repeatedly until the application is closed.
  auto callback = [](void* arg) {
    Application* pApp = reinterpret_cast<Application*>(arg);
    pApp->MainLoop();
    };
  emscripten_set_main_loop_arg(callback, &app, 0, true);
#else
  while (app.IsRunning()) {
    app.MainLoop();
  }
#endif // __EMSCRIPTEN__

  app.Terminate();
  return 0;

}

bool Application::Initialize() {
  // Start by opening a window.
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // NO_API bc We don't want OpenGL, we'll use WebGPU instead.
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // No resizing at this step of the guide
  window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

#ifdef __EMSCRIPTEN__
  Instance instance = createInstance();
#else
  InstanceDescriptor instanceDesc = {};
  Instance instance = createInstance(instanceDesc);
#endif
  // Check if the instance was created successfully
  if (!instance) {
    std::cerr << "Failed to create WebGPU instance. Could not initialize WebGPU" << std::endl;
    return false;
  }

  // Capture surface here so we can use in the main loop
  surface = glfwGetWGPUSurface(instance, window);

  std::cout << "Requesting adapter..." << std::endl;
  RequestAdapterOptions adapterOpts = {};
  adapterOpts.compatibleSurface = surface;

  Adapter adapter = instance.requestAdapter(adapterOpts);
  std::cout << "Got adapter: " << adapter << std::endl;

  // It is good practice to release the instance as soon as we have the adapter.
  instance.release();

  std::cout << "Requesting device..." << std::endl;
  DeviceDescriptor deviceDesc = {};
  deviceDesc.label = "My Device"; // anything works here, that's your call
  deviceDesc.requiredFeatureCount = 0; // we do not require any specific feature
  deviceDesc.requiredLimits = nullptr; // we do not require any specific limit
  deviceDesc.defaultQueue.nextInChain = nullptr;
  deviceDesc.defaultQueue.label = "The default queue";
  // A function that is invoked whenever the device stops being available.
  deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
    std::cout << "Device lost: reason " << reason;
    if (message) std::cout << " (" << message << ")";
    std::cout << std::endl;
    };

#ifdef __EMSCRIPTEN__
  deviceDesc.requiredLimits = nullptr;
#else
  RequiredLimits requiredLimits = GetRequiredLimits(adapter);
  deviceDesc.requiredLimits = &requiredLimits;
#endif

  device = adapter.requestDevice(deviceDesc);
  std::cout << "Got device: " << device << std::endl;

  // A function that is invoked whenever there is an error in the use of the device
  uncapturedErrorCallbackHandle = device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
    std::cout << "Uncaptured device error: type " << type;
    if (message) std::cout << " (" << message << ")";
    std::cout << std::endl;
    });

  queue = device.getQueue();

  // We configure the surface
  SurfaceConfiguration config = {};

  // Config of the textures created for the underlying swap chain
  config.width = 640;
  config.height = 480;
  config.usage = TextureUsage::RenderAttachment;
  surfaceFormat = surface.getPreferredFormat(adapter);
  config.format = surfaceFormat;

  // Here we do not use any particular view format yet
  config.viewFormatCount = 0;
  config.viewFormats = nullptr;
  config.device = device;
  config.presentMode = PresentMode::Fifo;
  config.alphaMode = CompositeAlphaMode::Auto;

  surface.configure(config);

  GetDepthTextureResources();

  // And equally good practice to release the adapter after it has been fully utilized
  adapter.release();

  InitializePipeline();

  InitializeBuffers();

  InitializeBindGroups();

  return true;
}

void Application::Terminate() {
  bindGroup.release();
  layout.release();
  bindGroupLayout.release();
  uniformBuffer.release();
  pointBuffer.release();
  indexBuffer.release();
  pipeline.release();
  surface.unconfigure();
  queue.release();
  surface.release();
  depthTextureView.release();
  depthTexture.release();
  device.release();
  glfwDestroyWindow(window);
  glfwTerminate();
}

void Application::MainLoop() {
  glfwPollEvents();

  // Update uniform buffer
  float time = static_cast<float>(glfwGetTime());
  // Only update the 1-st float of the buffer
  queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, time), &time, sizeof(float));

  //Update the model  Matrix
  float angle1 = time;
  glm::mat4 S = glm::scale(glm::mat4(1.0), glm::vec3(0.3f));
  glm::mat4 T1 = glm::translate(glm::mat4(1.0), glm::vec3(0.5, 0.0, 0.0));
  glm::mat4 R1 = glm::rotate(glm::mat4(1.0f), angle1, glm::vec3(0.0f, 0.0f, 1.0f));
  uniforms.modelMatrix = R1 * T1 * S;
  queue.writeBuffer(uniformBuffer, offsetof(MyUniforms, modelMatrix), &uniforms.modelMatrix, sizeof(MyUniforms::modelMatrix));

  // Get the next target texture/surface view
  TextureView targetView = GetNextSurfaceTextureView();
  if (!targetView) return;

  // Create the command encoder for the draw call
  CommandEncoderDescriptor encoderDesc = {};
  encoderDesc.label = "My command encoder";
  CommandEncoder encoder = device.createCommandEncoder(encoderDesc);

  // Create the render pass that clears the screen with our color (Equivalent to the usual glClearColor();
  RenderPassDescriptor renderPassDesc = {};

  // The attachment part of the render pass descriptor describes the target texture of the pass
  RenderPassColorAttachment renderPassColorAttachment = {};
  renderPassColorAttachment.view = targetView;
  renderPassColorAttachment.resolveTarget = nullptr;
  renderPassColorAttachment.loadOp = LoadOp::Clear;
  renderPassColorAttachment.storeOp = StoreOp::Store;
  renderPassColorAttachment.clearValue = WGPUColor{ 0.05, 0.05, 0.05, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
  renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // ! WGPU BACKEND

  // We now add a depth/stencil attachment:
  RenderPassDepthStencilAttachment depthStencilAttachment = {};
  // Setup depth/stencil attachment
  // The view of the depth texture
  depthStencilAttachment.view = depthTextureView;

  // The initial value of the depth buffer, meaning "far"
  depthStencilAttachment.depthClearValue = 1.0f;
  // Operation settings comparable to the color attachment
  depthStencilAttachment.depthLoadOp = LoadOp::Clear;
  depthStencilAttachment.depthStoreOp = StoreOp::Store;
  // we could turn off writing to the depth buffer globally here
  depthStencilAttachment.depthReadOnly = false;

  // Stencil setup, mandatory but unused
  depthStencilAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
  depthStencilAttachment.stencilLoadOp = LoadOp::Clear;
  depthStencilAttachment.stencilStoreOp = StoreOp::Store;
#else
  depthStencilAttachment.stencilLoadOp = LoadOp::Undefined;
  depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
  
#endif
  depthStencilAttachment.stencilReadOnly = true;


  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;
  renderPassDesc.depthStencilAttachment = &depthStencilAttachment;
  renderPassDesc.timestampWrites = nullptr;

  RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

  // Select which pipeline to use
  renderPass.setPipeline(pipeline);

  // Set vertex buffer while encoding the render pass
  renderPass.setVertexBuffer(0, pointBuffer, 0, pointBuffer.getSize());

  // The second argument must correspond to the choice of uint16_t or uint32_t when creating the index buffer, and to the index format specified in the pipeline.
  renderPass.setIndexBuffer(indexBuffer, IndexFormat::Uint16, 0, indexBuffer.getSize());

  //Set the bind group for the render pass
  renderPass.setBindGroup(0, bindGroup, 0, nullptr);

  // Draw 1 instance of 3 vertices, without an index buffer
  renderPass.drawIndexed(indexCount, 1, 0, 0, 0);

  renderPass.end();
  renderPass.release();

  // Finally encode and submit the render pass.
  CommandBufferDescriptor cmdBufferDesc = {};
  cmdBufferDesc.label = "Command Buffer";
  CommandBuffer command = encoder.finish(cmdBufferDesc);
  encoder.release();

  //std::cout << "Submitting command..." << std::endl;
  queue.submit(1, &command);
  command.release();
  //std::cout << "Command submitted." << std::endl;

  // At the end of the frame
  targetView.release();

#ifndef __EMSCRIPTEN__
  surface.present();
#endif

  // Tick/Poll the device but do not sleep for EMSCRIPTEN, do in the init callback instead.
#if defined(WEBGPU_BACKEND_DAWN)
  device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
  device.poll(false);
#endif
}

bool Application::IsRunning() {
  return !glfwWindowShouldClose(window);
}

TextureView Application::GetNextSurfaceTextureView() {
  // Get the surface Texture
  SurfaceTexture surfaceTexture;
  surface.getCurrentTexture(&surfaceTexture);
  if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success) {
    return nullptr;
  }
  Texture texture = surfaceTexture.texture;

  // Create a view for this surface texture
  TextureViewDescriptor viewDescriptor;
  viewDescriptor.label = "Surface Texture View";
  viewDescriptor.format = texture.getFormat();
  viewDescriptor.dimension = TextureViewDimension::_2D;
  viewDescriptor.baseMipLevel = 0;
  viewDescriptor.mipLevelCount = 1;
  viewDescriptor.baseArrayLayer = 0;
  viewDescriptor.arrayLayerCount = 1;
  viewDescriptor.aspect = TextureAspect::All;
  TextureView targetView = texture.createView(viewDescriptor);

#ifndef WEBGPU_BACKEND_WGPU
  // We no longer need the texture, only its view
  // NOTE: with wgpu-native, surface textures must not be manually released
  //Texture(surfaceTexture.texture).release(); // This looks like a leak?
  texture.release();
#endif // WEBGPU_BACKEND_WGPU

  return targetView;
}

void Application::GetDepthTextureResources() {
  // Create the depth texture
  TextureDescriptor depthTextureDesc;
  depthTextureDesc.dimension = TextureDimension::_2D;
  depthTextureDesc.format = depthTextureFormat;
  depthTextureDesc.mipLevelCount = 1;
  depthTextureDesc.sampleCount = 1;
  depthTextureDesc.size = { 640, 480, 1 };
  depthTextureDesc.usage = TextureUsage::RenderAttachment;
  depthTextureDesc.viewFormatCount = 1;
  depthTextureDesc.viewFormats = (WGPUTextureFormat*)&depthTextureFormat;
  depthTexture = device.createTexture(depthTextureDesc);

  // Create the view of the depth texture manipulated by the rasterizer
  TextureViewDescriptor depthTextureViewDesc;
  depthTextureViewDesc.aspect = TextureAspect::DepthOnly;
  depthTextureViewDesc.baseArrayLayer = 0;
  depthTextureViewDesc.arrayLayerCount = 1;
  depthTextureViewDesc.baseMipLevel = 0;
  depthTextureViewDesc.mipLevelCount = 1;
  depthTextureViewDesc.dimension = TextureViewDimension::_2D;
  depthTextureViewDesc.format = depthTextureFormat;
  depthTextureView = depthTexture.createView(depthTextureViewDesc);
}

void Application::InitializePipeline() {

  std::cout << "Creating shader module..." << std::endl;
  ShaderModule shaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", device);
  std::cout << "Shader module: " << shaderModule << std::endl;

  // Check for errors
  if (shaderModule == nullptr) {
    std::cerr << "Could not load shader!" << std::endl;
    exit(1);
  }

  // Create the render pipeline
  RenderPipelineDescriptor pipelineDesc;

  // Configure the vertex pipeline
  // We use one vertex buffer
  VertexBufferLayout vertexBufferLayout;
  // But we have now 2 different attributes
  std::vector<VertexAttribute> vertexAttribs(3);

  // For each attrib, describe its layout, aka how to interpret the data
  // Position attribute
  vertexAttribs[0].shaderLocation = 0;
  vertexAttribs[0].format = VertexFormat::Float32x3;
  vertexAttribs[0].offset = offsetof(VertexAttributes, position);

  // Normal attribute
  vertexAttribs[1].shaderLocation = 1;
  vertexAttribs[1].format = VertexFormat::Float32x3;
  vertexAttribs[1].offset = offsetof(VertexAttributes, normal);

  // Color attribute
  vertexAttribs[2].shaderLocation = 2;
  vertexAttribs[2].format = VertexFormat::Float32x3;
  vertexAttribs[2].offset = offsetof(VertexAttributes, color);

  vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
  vertexBufferLayout.attributes = vertexAttribs.data();

  // Common to attributes from the same buffer
  vertexBufferLayout.arrayStride = sizeof(VertexAttributes);
  vertexBufferLayout.stepMode = VertexStepMode::Vertex; // We move to the next vertex for each vertex shader invocation

  // We do not use any vertex buffer for this simple example.
  pipelineDesc.vertex.bufferCount = 1;
  pipelineDesc.vertex.buffers = &vertexBufferLayout;

  // NB: We define the 'shaderModule' in the second part of this chapter.
  // Here we tell that the programmable vertex shader stage is described
  // by the function called 'vs_main' in that module.
  pipelineDesc.vertex.module = shaderModule;
  pipelineDesc.vertex.entryPoint = "vs_main";
  pipelineDesc.vertex.constantCount = 0;
  pipelineDesc.vertex.constants = nullptr;

  // Each sequence of 3 vertices is considered as a triangle
  pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;

  // We'll see later how to specify the order in which vertices should be
  // connected. When not specified, vertices are considered sequentially.
  pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;

  // The face orientation is defined by assuming that when looking
  // from the front of the face, its corner vertices are enumerated
  // in the counter-clockwise (CCW) order.
  pipelineDesc.primitive.frontFace = FrontFace::CCW;

  // But the face orientation does not matter much because we do not
  // cull (i.e. "hide") the faces pointing away from us (which is often
  // used for optimization).
  pipelineDesc.primitive.cullMode = CullMode::None;

  // We tell that the programmable fragment shader stage is described
  // by the function called 'fs_main' in the shader module.
  FragmentState fragmentState;
  fragmentState.module = shaderModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.constantCount = 0;
  fragmentState.constants = nullptr;

  BlendState blendState;
  blendState.color.srcFactor = BlendFactor::SrcAlpha;
  blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
  blendState.color.operation = BlendOperation::Add;
  blendState.alpha.srcFactor = BlendFactor::Zero;
  blendState.alpha.dstFactor = BlendFactor::One;
  blendState.alpha.operation = BlendOperation::Add;

  ColorTargetState colorTarget;
  colorTarget.format = surfaceFormat;
  colorTarget.blend = &blendState;
  colorTarget.writeMask = ColorWriteMask::All; // We could write to only some of the color channels.

  // We have only one target because our render pass has only one output color
  // attachment.
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;
  pipelineDesc.fragment = &fragmentState;

  // Setup depth state
  DepthStencilState depthStencilState = Default;
  depthStencilState.depthCompare = CompareFunction::Less;
  depthStencilState.depthWriteEnabled = true;

  depthStencilState.format = depthTextureFormat;
  depthStencilState.stencilReadMask = 0;
  depthStencilState.stencilWriteMask = 0;

  pipelineDesc.depthStencil = &depthStencilState;

  // Samples per pixel
  pipelineDesc.multisample.count = 1;

  // Default value for the mask, meaning "all bits on"
  pipelineDesc.multisample.mask = ~0u;

  // Default value as well (irrelevant for count = 1 anyways)
  pipelineDesc.multisample.alphaToCoverageEnabled = false;

  // Define binding layout (don't forget to = Default)
  BindGroupLayoutEntry bindingLayout = Default;
  // The binding index as used in the @binding attribute in the shader
  bindingLayout.binding = 0;
  // The stage(s) that needs to access this resource(s)
  bindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
  bindingLayout.buffer.type = BufferBindingType::Uniform;
  bindingLayout.buffer.minBindingSize = sizeof(MyUniforms);

  // Create a bind group layout
  BindGroupLayoutDescriptor bindGroupLayoutDesc{};
  bindGroupLayoutDesc.entryCount = 1;
  bindGroupLayoutDesc.entries = &bindingLayout;
  bindGroupLayout = device.createBindGroupLayout(bindGroupLayoutDesc);

  // Create the pipeline layout
  PipelineLayoutDescriptor layoutDesc{};
  layoutDesc.bindGroupLayoutCount = 1;
  layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&bindGroupLayout;
  layout = device.createPipelineLayout(layoutDesc);

  pipelineDesc.layout = layout;

  pipeline = device.createRenderPipeline(pipelineDesc);

  // We no longer need to access the shader module
  shaderModule.release();
}

RequiredLimits Application::GetRequiredLimits(Adapter adapter) const {
  // Get adapter supported limits, in case we need them
  SupportedLimits supportedLimits;
  adapter.getLimits(&supportedLimits);

  // NOTE: is "Default" just an alias for a default initializer {} ? It crashes if I don't copy the supported limits manually.
  RequiredLimits requiredLimits = Default;
  requiredLimits.limits = supportedLimits.limits; // Start with the supported limits as a base, then override the ones we want to require

  // We use at most 3 vertex attribute for now
  requiredLimits.limits.maxVertexAttributes = 3;

  // We should also tell that we use 1 vertex buffer
  requiredLimits.limits.maxVertexBuffers = 1;
  // Maximum size of a buffer is 6 vertices of 5 float each.
  requiredLimits.limits.maxBufferSize = 16 * sizeof(VertexAttributes);
  // Maximum stride between 2 consecutive vertices in the vertex buffer
  requiredLimits.limits.maxVertexBufferArrayStride = sizeof(VertexAttributes);

  // There is a maximum of 3 float forwarded from vertex to fragment shader
  requiredLimits.limits.maxInterStageShaderComponents = 6;

  // We use at most 1 bind group for now
  requiredLimits.limits.maxBindGroups = 1;
  // We use at most 1 uniform buffer per stage
  requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
  // Uniform structs have a size of maximum 16 float (more than what we need)
  requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);

  // For the depth buffer, we enable textures (up to the size of the window):
  requiredLimits.limits.maxTextureDimension1D = 480;
  requiredLimits.limits.maxTextureDimension2D = 640;
  requiredLimits.limits.maxTextureArrayLayers = 1;

  return requiredLimits;
}

void Application::InitializeBuffers() {

  // Define data vectors, but without filling them in
  std::vector<float> pointData;
  std::vector<uint16_t> indexData;

  // Here we use the new 'loadGeometry' function:
  bool success = ResourceManager::loadGeometry(RESOURCE_DIR "/pyramid.txt", pointData, indexData, 6);

  // Check for errors
  if (!success) {
    std::cerr << "Could not load geometry!" << std::endl;
    exit(1);
  }

  indexCount = static_cast<uint32_t>(indexData.size());

  // Create vertex buffer
  BufferDescriptor bufferDesc;
  bufferDesc.size = pointData.size() * sizeof(float);
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex; // Vertex usage here!
  bufferDesc.mappedAtCreation = false;
  pointBuffer = device.createBuffer(bufferDesc);

  // Upload geometry data to the buffer
  queue.writeBuffer(pointBuffer, 0, pointData.data(), bufferDesc.size);

  // Create index buffer
  // (we reuse the bufferDesc initialized for the pointBuffer)
  bufferDesc.size = indexData.size() * sizeof(uint16_t);
  bufferDesc.size = (bufferDesc.size + 3) & ~3; // round up to the next multiple of 4
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Index;
  indexBuffer = device.createBuffer(bufferDesc);

  queue.writeBuffer(indexBuffer, 0, indexData.data(), bufferDesc.size);

  // Create uniform buffer (reusing object and updating internal data)
  // The buffer is structured so the internal members are aligned to 16 bytes, so we can just use the size of the struct.
  bufferDesc.size = sizeof(MyUniforms);

  // Make sure to flag the buffer as BufferUsage::Uniform
  bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;

  bufferDesc.mappedAtCreation = false;
  uniformBuffer = device.createBuffer(bufferDesc);

  // Upload the initial value of the uniforms
  uniforms.time = 1.0f;
  uniforms.color = { 0.0f, 1.0f, 0.4f, 1.0f };
  uniforms.modelMatrix = glm::mat4(1.0f);
  uniforms.viewMatrix = glm::lookAt(glm::vec3(-2.0f, -3.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  uniforms.projectionMatrix = glm::perspective(glm::radians(45.0f), 640.0f / 480.0f, 0.1f, 100.0f);
  queue.writeBuffer(uniformBuffer, 0, &uniforms, sizeof(MyUniforms));
}

void Application::InitializeBindGroups() {
  // Create a binding
  BindGroupEntry binding{};
  // The index of the binding (the entries in bindGroupDesc can be in any order)
  binding.binding = 0;
  // The buffer it is actually bound to
  binding.buffer = uniformBuffer;
  // We can specify an offset within the buffer, so that a single buffer can hold
  // multiple uniform blocks.
  binding.offset = 0;
  // And we specify again the size of the buffer.
  binding.size = sizeof(MyUniforms);

  // A bind group contains one or multiple bindings
  BindGroupDescriptor bindGroupDesc{};
  bindGroupDesc.layout = bindGroupLayout;
  // There must be as many bindings as declared in the layout!
  bindGroupDesc.entryCount = 1;
  bindGroupDesc.entries = &binding;
  bindGroup = device.createBindGroup(bindGroupDesc);
}