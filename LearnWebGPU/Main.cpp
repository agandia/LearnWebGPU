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

#include "Application.h"

int main(int, char**) {
  Application app;
  if (!app.onInit()) {
    std::cerr << "Failed to initialize the application. Program terminated" << std::endl;
    return 1;
  }

#ifdef __EMSCRIPTEN__
  // In the Emscripten backend, we use emscripten_set_main_loop to call the main loop function repeatedly until the application is closed.
  auto callback = [](void* arg) {
    Application* pApp = reinterpret_cast<Application*>(arg);
    pApp->onFrame();
    };
  emscripten_set_main_loop_arg(callback, &app, 0, true);
#else
  while (app.isRunning()) {
    app.onFrame();
  }
#endif // __EMSCRIPTEN__

  app.onFinish();
  return 0;
}