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

// NOTE: Use for debug only.
//#include "webgpu-utils.h"

// Replaced default <webgpu/webgpu.hpp> with the webgpu.hpp version,
// containing some syntactic sugar and C++ wrappers
#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu/webgpu.hpp"

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif // __EMSCRIPTEN__

#include <iostream>
#include <cassert>
#include <vector>

// use wgpu namespace for convenience
using namespace wgpu;

// We embbed the shader source here
const char * shaderSource = R"(
  @vertex
  fn vs_main(@location(0) in_vertex_position: vec2f ) -> @builtin(position) vec4f {    
    return vec4f(in_vertex_position, 0.0, 1.0);
  }
  @fragment
  fn fs_main() -> @location(0) vec4f {
    return vec4f(0.0, 0.4, 1.0, 1.0);
  }
)";

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
    TextureView GetNextSurfaceTextureView();

    // Substep of Initialize, never expose
    void InitializePipeline();
    RequiredLimits GetRequiredLimits(Adapter adapter) const;
    // Just for the sake of this example
    void InitializeBuffers();

    // Shared data between init and main loop.
    GLFWwindow* window;
    Device device;
    Queue queue;
    Surface surface;
    TextureFormat surfaceFormat = TextureFormat::Undefined;
    RenderPipeline pipeline;
    Buffer vertexBuffer;
    uint32_t vertexCount;

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

  InstanceDescriptor instanceDesc = {};
	Instance instance = createInstance(instanceDesc);

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
  RequiredLimits requiredLimits = GetRequiredLimits(adapter);
  deviceDesc.requiredLimits = &requiredLimits;  
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

  // And equally good practice to release the adapter as soon as we have the device and have configured the surface/underlying swapchain.
  adapter.release();

  InitializePipeline();

  InitializeBuffers();

  return true;
}

void Application::Terminate() {
  vertexBuffer.release();
  pipeline.release();
  surface.unconfigure();
  queue.release();
  surface.release();
  device.release();
  glfwDestroyWindow(window);
  glfwTerminate();
}

void Application::MainLoop() {
  glfwPollEvents();

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
  renderPassColorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
  renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // ! WGPU BACKEND

  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;
  renderPassDesc.depthStencilAttachment = nullptr;
  renderPassDesc.timestampWrites = nullptr;

  RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

  // Select which pipeline to use
  renderPass.setPipeline(pipeline);

  // Set vertex buffer while encoding the render pass
  renderPass.setVertexBuffer(0, vertexBuffer, 0, vertexBuffer.getSize());

  // Draw 1 instance of 3 vertices, without an index buffer
  renderPass.draw(vertexCount, 1, 0, 0);

  renderPass.end();
  renderPass.release();

  // Finally encode and submit the render pass.
  CommandBufferDescriptor cmdBufferDesc = {};
  cmdBufferDesc.label = "Command Buffer";
  CommandBuffer command = encoder.finish(cmdBufferDesc);
  encoder.release();

  std::cout << "Submitting command..." << std::endl;
  queue.submit(1, &command);
  command.release();
  std::cout << "Command submitted." << std::endl;

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

void Application::InitializePipeline() {
  // Load shader modules
  ShaderModuleDescriptor shaderDesc;
#ifdef WEBGPU_BACKEND_WGPU
  shaderDesc.hintCount = 0;
  shaderDesc.hints = nullptr;
#endif // WEBGPU_BACKEND_WGPU

  // We use the extension mechanism to specify the WGSL part of the shader module descriptor
  ShaderModuleWGSLDescriptor shaderCodeDesc;
  // Set the chained struct's header
  shaderCodeDesc.chain.next = nullptr;
  shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
  // Connect the chain
  shaderDesc.nextInChain = &shaderCodeDesc.chain;
  shaderCodeDesc.code = shaderSource;
  ShaderModule shaderModule = device.createShaderModule(shaderDesc);

  // Create the render pipeline
  RenderPipelineDescriptor pipelineDesc;

  // Configure the vertex pipeline
  // We use one vertex buffer
  VertexBufferLayout vertexBufferLayout;
  VertexAttribute positionAttribute;
  // For each attrib, describe its layout, aka how to interpret the data
  // Corresponds to the @location(0) in the vertex shader
  positionAttribute.shaderLocation = 0;
  // The vertex shader expects a vec2f, which is 2 32-bit floats
  positionAttribute.format = VertexFormat::Float32x2;
  // Index of the first component
  positionAttribute.offset = 0;

  vertexBufferLayout.attributeCount = 1;
  vertexBufferLayout.attributes = &positionAttribute;

  // Common to attributes from the same buffer
  vertexBufferLayout.arrayStride = 2 * sizeof(float); // Each vertex is a vec2f, so 2 floats
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

  // We do not use stencil/depth testing for now
  pipelineDesc.depthStencil = nullptr;

  // Samples per pixel
  pipelineDesc.multisample.count = 1;

  // Default value for the mask, meaning "all bits on"
  pipelineDesc.multisample.mask = ~0u;

  // Default value as well (irrelevant for count = 1 anyways)
  pipelineDesc.multisample.alphaToCoverageEnabled = false;
  pipelineDesc.layout = nullptr;

  pipeline = device.createRenderPipeline(pipelineDesc);

  // We no longer need to access the shader module
  shaderModule.release();
}

RequiredLimits Application::GetRequiredLimits(Adapter adapter) const {
  // Get adapter supported limits, in case we need them
  SupportedLimits supportedLimits;
  adapter.getLimits(&supportedLimits);

  // NOTE: is "Default" just an alias for a default initializer {} ?
  RequiredLimits requiredLimits = Default;
  requiredLimits.limits = supportedLimits.limits; // Start with the supported limits as a base, then override the ones we want to require

  // We use at most 1 vertex attribute for now
  requiredLimits.limits.maxVertexAttributes = 1;
  // We should also tell that we use 1 vertex buffer
  requiredLimits.limits.maxVertexBuffers = 1;
  // Maximum size of a buffer is 6 vertices of 2 float each.
  requiredLimits.limits.maxBufferSize = 6 * 2 * sizeof(float);
  // Maximum stride between 2 consecutive vertices in the vertex buffer
  requiredLimits.limits.maxVertexBufferArrayStride = 2 * sizeof(float);

  // These two limits are different because they are "minimum" limits
  requiredLimits.limits.minUniformBufferOffsetAlignment = supportedLimits.limits.minUniformBufferOffsetAlignment;
  requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;

  return requiredLimits;
}

void Application::InitializeBuffers() {
  
  // Vertex Buffer Data
  std::vector<float> vertexData = {
    // For our example we define one triangle
    -0.5f, -0.5f, // Vertex 1: position (x, y)
     0.5f, -0.5f, // Vertex 2: position (x, y)
     0.0f,  0.5f, // Vertex 3: position (x, y)

    // And a second triangle
    -0.55f, -0.5f,// Vertex 1: position (x, y)
    -0.05f,  0.5f,// Vertex 2: position (x, y)
    -0.55f,  0.5f // Vertex 3: position (x, y)

  };
  vertexCount = static_cast<uint32_t>(vertexData.size() / 2); // Each vertex has 2 components (x, y)

  // Create vertex Buffer
  BufferDescriptor bufferDesc = {};
  bufferDesc.label = "Some GPU-side data buffer";
  bufferDesc.usage = BufferUsage::CopyDst| BufferUsage::Vertex;
  bufferDesc.size = vertexData.size() * sizeof(float);
  bufferDesc.mappedAtCreation = false;
  vertexBuffer = device.createBuffer(bufferDesc);

  // copy this from numbers (Inside RAM) to buffer1 (VRAM)
  queue.writeBuffer(vertexBuffer, 0, vertexData.data(), bufferDesc.size);

}