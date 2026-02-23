# CMakeList.txt : CMake project for LearnWebGPU, include source and define
# project specific logic here.
#

# Include webgpu directory, to define the "webgpu" target."
add_subdirectory(webgpu)

# Add source to this project's executable.
add_executable (LearnWebGPU "Main.cpp" "webgpu-utils.h" "webgpu-utils.cpp")

# Add the "webgpu" target as a dependency of the executable
target_link_libraries(LearnWebGPU PRIVATE webgpu)

# The appliucation's binary must find wgpu.dll or libwgpu.so at runtime,
# so we copy it (called WPU_RUNTIME_LIB in general)
# next to the binary
target_copy_webgpu_binaries(LearnWebGPU)

# Set C++ standard and compile options.
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET LearnWebGPU PROPERTY 
    CXX_STANDARD 20 
    CXX_STANDARD_REQUIRED ON 
    CXX_EXTENSIONS OFF 
    COMPILE_WARNING_AS_ERROR ON
  )
endif()

# Set compiler warnings to be treated as errors and enable extra warnings.
if(MSVC)
  target_compile_options(LearnWebGPU PRIVATE /W4)
else()
  target_compile_options(LearnWebGPU PRIVATE -Wall -Wextra -pedantic)
endif()

if(XCODE)
  set_target_properties(LearnWebGPU PROPERTIES 
    XCODE_GENERATE_SCHEME ON 
    XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Metal"
  )
endif()

# Options that are specific to EMSCRIPTEN
if (EMSCRIPTEN)
  # Generate a full webpage rather than a simple WebAssembly module.
  set_target_properties(LearnWebGPU PROPERTIES SUFFIX ".html")
  # Enable the use of emscripten_sleep()
	target_link_options(App PRIVATE -sASYNCIFY)
endif()

# TODO: Add tests and install targets if needed.
