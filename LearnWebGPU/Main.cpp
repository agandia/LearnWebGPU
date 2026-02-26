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
    //                    ^^^ 2. We get the address of the app in the callback.
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

  // And equally good practice to release the adapter as soon as we have the device.
  wgpuAdapterRelease(adapter);

  // A function that is invoked whenever there is an error in the use of the device
  auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
    std::cout << "Uncaptured device error: type " << type;
    if (message) std::cout << " (" << message << ")";
    std::cout << std::endl;
    };
  wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr /* pUserData */);

  queue = wgpuDeviceGetQueue(device);
  return true;
}

void Application::Terminate() {
  wgpuQueueRelease(queue);
  wgpuSurfaceRelease(surface);
  wgpuDeviceRelease(device);
  glfwDestroyWindow(window);
  glfwTerminate();
}

void Application::MainLoop() {
  glfwPollEvents();

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