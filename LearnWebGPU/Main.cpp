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
#include "webgpu-utils.h"

#include <webgpu/webgpu.h>
#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif // WEBGPU_BACKEND_WGPU
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif // __EMSCRIPTEN__

#include <iostream>
#include <cassert>
#include <vector>

class Application {
  public:
    // Initialize all or catch errors.
    bool Initialize();

    // Clean everything
    void Terminate();

    // Draw a frame and handle events
    void MainLoop();

    // Returns true if the application should keep running, false if it should exit.
    bool IsRunning();

  private:
    WGPUTextureView GetNextSurfaceTextureView();

    // Shared data between initi and main loop.
    GLFWwindow* window;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSurface surface;
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
    //                     ^^^ 2. We get the address of the app in the callback.
    Application* pApp = reinterpret_cast<Application*>(arg);
    //                  ^^^^^^^^^^^^^^^^ 3. We force this address to be interpreted
    //                                      as a pointer to an Application object.
    pApp->MainLoop(); // 4. We can use the application object
    };
  emscripten_set_main_loop_arg(callback, &app, 0, true);
  //                                     ^^^^ 1. We pass the address of our application object.
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
  window = glfwCreateWindow(800, 600, "Learn WebGPU", nullptr, nullptr);

	// We create the descriptor
  WGPUInstanceDescriptor desc = {};
  desc.nextInChain = nullptr;

  // We create the instance using this descriptor
#ifdef WEBGPU_BACKEND_EMSCRIPTEN
  WGPUInstance instance = wgpuCreateInstance(nullptr);
  std::cout << "EMSCRIPTEN BACKENDED INSTANCE" << std::endl;
#else
  WGPUInstance instance = wgpuCreateInstance(&desc);
  std::cout << "WGPU/DAWN BACKENDED INSTANCE" << std::endl;
#endif // WEBGPU_BACKEND_EMSCRIPTEN

  // Check if the instance was created successfully
  if (!instance) {
    std::cerr << "Failed to create WebGPU instance. Could not initialize WebGPU" << std::endl;
    return false;
  }

  std::cout << "Requesting adapter..." << std::endl;
  // Capture surface here so we can use in the main loop
  surface = glfwGetWGPUSurface(instance, window);

  WGPURequestAdapterOptions adapterOpts = {};
  adapterOpts.nextInChain = nullptr;
  adapterOpts.compatibleSurface = surface;

  WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);
  std::cout << "Got adapter: " << adapter << std::endl;

  // It is good practice to release the instance as soon as we have the adapter.
  wgpuInstanceRelease(instance);

  std::cout << "Requesting device..." << std::endl;
  WGPUDeviceDescriptor deviceDesc = {};
  deviceDesc.nextInChain = nullptr;
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

  device = requestDeviceSync(adapter, &deviceDesc);
  std::cout << "Got device: " << device << std::endl;

  // A function that is invoked whenever there is an error in the use of the device
  auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
    std::cout << "Uncaptured device error: type " << type;
    if (message) std::cout << " (" << message << ")";
    std::cout << std::endl;
    };
  wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr /* pUserData */);

  queue = wgpuDeviceGetQueue(device);

  // We configure the surface
  WGPUSurfaceConfiguration config = {};
  config.nextInChain = nullptr;

  // Config of the textures created for the underlying swap chain
  config.width = 640;
  config.height = 480;
  config.usage = WGPUTextureUsage_RenderAttachment;
  WGPUTextureFormat surfaceFormat = wgpuSurfaceGetPreferredFormat(surface, adapter);
  config.format = surfaceFormat;

  // Here we do not use any particular view format yet
  config.viewFormatCount = 0;
  config.viewFormats = nullptr;
  config.device = device;
  config.presentMode = WGPUPresentMode_Fifo;
  config.alphaMode = WGPUCompositeAlphaMode_Auto;

  wgpuSurfaceConfigure(surface, &config);

  // And equally good practice to release the adapter as soon as we have the device and have configured the surface/underlying swapchain.
  wgpuAdapterRelease(adapter);  

  return true;
}

void Application::Terminate() {
  wgpuSurfaceUnconfigure(surface);
  wgpuQueueRelease(queue);
  wgpuSurfaceRelease(surface);
  wgpuDeviceRelease(device);
  glfwDestroyWindow(window);
  glfwTerminate();
}

void Application::MainLoop() {
  glfwPollEvents();

  // Get the next target texture/surface view
  WGPUTextureView targetView = GetNextSurfaceTextureView();
  if (!targetView) return;

  // Create the command encoder for the draw call
  WGPUCommandEncoderDescriptor encoderDesc = {};
  encoderDesc.nextInChain = nullptr;
  encoderDesc.label = "My command encoder";
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

  // Create the render pass that clears the screen with our color (Equivalent to the usual glClearColor();
  WGPURenderPassDescriptor renderPassDesc = {};
  renderPassDesc.nextInChain = nullptr;

  // The attachment part of the render pass descpritor describes the target texture of the pass
  WGPURenderPassColorAttachment renderPassColorAttachment = {};
  renderPassColorAttachment.view = targetView;
  renderPassColorAttachment.resolveTarget = nullptr;
  renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
  renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
  renderPassColorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
  renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // ! WGPU BACKEND

  renderPassDesc.colorAttachmentCount = 1;
  renderPassDesc.colorAttachments = &renderPassColorAttachment;
  renderPassDesc.depthStencilAttachment = nullptr;
  renderPassDesc.timestampWrites = nullptr;

  // Create the render pass and end it immediatelly, for now we don't draw anything, besides clearing the screen
  WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
  wgpuRenderPassEncoderEnd(renderPass);
  wgpuRenderPassEncoderRelease(renderPass);

  // Finally encode and submit the render pass.
  WGPUCommandBufferDescriptor cmdBufferDesc = {};
  cmdBufferDesc.nextInChain = nullptr;
  cmdBufferDesc.label = "Command Buffer";
  WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);

  std::cout << "Submitting command..." << std::endl;
  wgpuQueueSubmit(queue, 1, &command);
  wgpuCommandBufferRelease(command);
  std::cout << "Command submitted." << std::endl;

  // At the end of the frame
  wgpuTextureViewRelease(targetView);
#ifndef __EMSCRIPTEN__
  wgpuSurfacePresent(surface);
#endif

  // Tick/Poll the device but do not sleep for EMSCRIPTEN, do in the init callback instead.
#if defined(WEBGPU_BACKEND_DAWN)
  wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
  wgpuDevicePoll(device, false, nullptr);
#endif
}

bool Application::IsRunning() {
  return !glfwWindowShouldClose(window);
}

WGPUTextureView Application::GetNextSurfaceTextureView() {
    // Get the surface Texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        return nullptr;
    }

    // Create a view for this surface texture
    WGPUTextureViewDescriptor viewDescriptor;
    viewDescriptor.nextInChain = nullptr;
    viewDescriptor.label = "Surface Texture View";
    viewDescriptor.format = wgpuTextureGetFormat(surfaceTexture.texture);
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect = WGPUTextureAspect_All;
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDescriptor);

#ifndef WEBGPU_BACKEND_WGPU
    // We no longer need the texture, only its view
    // NOTE: with wgpu-native, surface textures must not be manually released
    wgpuTextureRelease(surfaceTexture.texture);
#endif // WEBGPU_BACKEND_WGPU

    return targetView;
}