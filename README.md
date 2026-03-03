# LearnWebGPU

#Building the project
 As this becomes a bit more convoluted, I feel the need to write donw some instructions to build and run as a future reference
## Building native
 In MSVC, it should suffice to press Ctrl + Shift + B to rebuild clean.
 From a console
```
 cmake . -B build
 cmake --build build
```

## Building for the web
 Here I had to go over several hurdles, for which I haven't found the perfect solution yet.
 
 First make sure you have installed a Linux terminal (For example WSL Ubuntu is fairly accessible)
 Instal emsdk following the instructions here, this is a 1 time thing only.
 https://emscripten.org/docs/getting_started/downloads.html
 
```
 # Download and install the latest SDK tools.
 ./emsdk install latest

 # Make the "latest" SDK "active" for the current user. (writes .emscripten file)
 ./emsdk activate latest
```

 Instead of installing latest, you might want to instal version 3.1.61, same for activation,
 unless you are ready to source and patch the webgpu code to the latest emscripten compatible version.
 
 Download python and cmake too.
 
 Make sure to source emsdk_env to your PATH
```
 # Activate PATH and other environment variables in the current terminal
 source ./emsdk_env.sh
``` 
 Now, for the actual build
``` 
 rm -rf build-web # Optional, only when rebuilding to make sure you clear the workspace
 emcc --clear-cache
 emcmake cmake -B build-web
``` 
 This will generate your build-web folder, here the cmake files provided from the original repo will generate the webgpu.hpp wrapper
 in consonance to emscripten version 3.1.61 but I found that the tutorial examples and that version are slightly off sync.
 
 Find the file here
``` 
 [Your_Path]\build-web\_deps\webgpu-backend-emscripten-src\include\webgpu\webgpu.hpp
``` 
 
 When comparing this file to the webgpu.hpp wrapper for native builds:
 
``` 
 From console
 [Your_Path]\build\_deps\webgpu-backend-emscripten-src\include\webgpu\webgpu.hpp
 From MSVC
 [Your_Path]\out\build\x64-debug\_deps\webgpu-backend-wgpu-src\include\webgpu
``` 
 There is a createInstance overload that is missing and prevents us from building the code.
 So go into the build-web generated one and find the following
``` 
 Instance createInstance(const InstanceDescriptor& descriptor);

#ifdef WEBGPU_CPP_IMPLEMENTATION

 Instance createInstance(const InstanceDescriptor& descriptor) {
	return wgpuCreateInstance(&descriptor);
 }
``` 

 Replace that block with
``` 
 Instance createInstance();
Instance createInstance(const InstanceDescriptor& descriptor);

#ifdef WEBGPU_CPP_IMPLEMENTATION

Instance createInstance() {
	return wgpuCreateInstance(nullptr);
}

Instance createInstance(const InstanceDescriptor& descriptor) {
	return wgpuCreateInstance(&descriptor);
}

``` 

 I am looking into this issue and trying to automate it.

 Finally you are ready to make the build, running this command from the WSL console
``` 
 cmake --build build-web
``` 

 If you installed python, you should then be able to self host the generated .html page and open it in a browser
 ``` 
  python3 -m http.server -d build-web
 ``` 
 
# Running the code
## Native
 In MSVC simply press F5
 From a windows command prompt run `./build/LearnWebGPU/LearnWebGPU.exe`
## Web
 Go to http://localhost:8000/LearnWebGPU/LearnWebGPU.html 