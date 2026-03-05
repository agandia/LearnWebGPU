#include "Application.h"
#include "ResourceManager.h"

#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <iostream>
#include <cassert>
#include <filesystem>
#include <sstream>
#include <string>

using namespace wgpu;

bool Application::onInit()
{
  // Initialization battery test
  if (!initWindowAndDevice()) return false;
	configSurface();
  if (!initDepthBuffer()) return false;
  if (!initRenderPipeline()) return false;
  if (!initTexture()) return false;
  if (!initGeometry()) return false;
  if (!initUniforms()) return false;
  if (!initBindGroup()) return false;
  return true;
}

void Application::onFrame()
{
	glfwPollEvents();
	updateDragInertia();

	// Update any uniforms that require new values each frame.
	float time = static_cast<float>(glfwGetTime());
	// Only update the 1-st float of the buffer
	mQueue.writeBuffer(mUniformBuffer, offsetof(BasicShaderUniforms, time), &time, sizeof(float));

	//Update the model  Matrix
	float angle1 = time;
	glm::mat4 M(1.0f);
	M = glm::rotate(M, angle1, glm::vec3(0.0f, 0.0f, 1.0f));
	M = glm::translate(M, glm::vec3(0.0f, 0.0f, 0.0f));
	M = glm::scale(M, glm::vec3(0.3f));
	mUniforms.modelMatrix = M;

	mQueue.writeBuffer(mUniformBuffer, offsetof(BasicShaderUniforms, modelMatrix), &mUniforms.modelMatrix, sizeof(BasicShaderUniforms::modelMatrix));

	//float viewZ = glm::mix(0.0f, 0.25f, glm::cos(2 * glm::pi<float>() * time / 4.0f) * 0.5f + 0.5f);
	//mUniforms.viewMatrix = glm::lookAt(glm::vec3(-0.5f, -1.5f, viewZ + 0.25f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	//mQueue.writeBuffer(mUniformBuffer, offsetof(BasicShaderUniforms, viewMatrix), &mUniforms.viewMatrix, sizeof(BasicShaderUniforms::viewMatrix));
	
	TextureView nextTexture = getNextSurfaceTextureView();
	if (!nextTexture) {
		std::cerr << "Cannot acquire next swap chain texture" << std::endl;
		return;
	}

	CommandEncoderDescriptor commandEncoderDesc{};
	commandEncoderDesc.label = "Command Encoder";
	CommandEncoder encoder = mDevice.createCommandEncoder(commandEncoderDesc);

	RenderPassDescriptor renderPassDesc{};

	RenderPassColorAttachment renderPassColorAttachment{};
	renderPassColorAttachment.view = nextTexture;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = LoadOp::Clear;
	renderPassColorAttachment.storeOp = StoreOp::Store;
	renderPassColorAttachment.clearValue = Color{ 0.30, 0.30, 0.30, 1.0 };
#ifndef WEBGPU_BACKEND_WGPU
	renderPassColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif // ! WGPU BACKEND
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;

	RenderPassDepthStencilAttachment depthStencilAttachment{};
	depthStencilAttachment.view = mDepthTextureView;
  depthStencilAttachment.depthClearValue = 1.0f; // clear to "far"
	depthStencilAttachment.depthLoadOp = LoadOp::Clear;
	depthStencilAttachment.depthStoreOp = StoreOp::Store;
	depthStencilAttachment.depthReadOnly = false;
	depthStencilAttachment.stencilClearValue = 0;
#ifdef WEBGPU_BACKEND_WGPU
	depthStencilAttachment.stencilLoadOp = LoadOp::Clear;
	depthStencilAttachment.stencilStoreOp = StoreOp::Store;
#else
	depthStencilAttachment.stencilLoadOp = LoadOp::Undefined;
	depthStencilAttachment.stencilStoreOp = StoreOp::Undefined;
#endif // ! WGPU BACKEND
	depthStencilAttachment.stencilReadOnly = true;

	renderPassDesc.depthStencilAttachment = &depthStencilAttachment;
	renderPassDesc.timestampWrites = nullptr;

	RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

	renderPass.setPipeline(mPipeline);

	renderPass.setVertexBuffer(0, mVertexBuffer, 0, mVertexCount * sizeof(ResourceManager::VertexAttributes));

	// Set binding group
	renderPass.setBindGroup(0, mBindGroup, 0, nullptr);

	renderPass.draw(mVertexCount, 1, 0, 0);
  //renderPass.drawIndexed(mVertexCount, 1, 0, 0, 0); // <- Alternative for indexed

	renderPass.end();
	renderPass.release();

	nextTexture.release();

	CommandBufferDescriptor cmdBufferDescriptor{};
	cmdBufferDescriptor.label = "Command buffer";
	CommandBuffer command = encoder.finish(cmdBufferDescriptor);
	encoder.release();
	mQueue.submit(command);
	command.release();

#ifndef __EMSCRIPTEN__
	mSurface.present();
#endif // ! __EMSCRIPTEN__

#if defined(WEBGPU_BACKEND_DAWN)
	device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
	mDevice.poll(false);
#endif

}

void Application::onFinish()
{
  // Each part of the renderer takes care of cleaning up after itself, call in reverse order
  terminateBindGroup();
  terminateUniforms();
  terminateGeometry();
  terminateTexture();
  terminateRenderPipeline();
  terminateDepthBuffer();
  terminateSurfaceConfig();
  terminateWindowAndDevice();
}

bool Application::isRunning()
{
  return !glfwWindowShouldClose(mWindow);
}

// When resizing, we need to re-create the swap chain and depth buffer with the new size.
void Application::onResize()
{
	// Add a ward
	if (mWindowWidth <= 0 || mWindowHeight <= 0) return;

	// Terminate in reverse order
	terminateDepthBuffer();
	terminateSurfaceConfig();

	// Re-init
	configSurface();
	initDepthBuffer();

  updateProjectionMatrix();
}

void Application::onMouseMove(double xpos, double ypos)
{
	if (mDragState.active) {
		glm::vec2 currentMouse = glm::vec2(-(float)xpos, (float)ypos);
		glm::vec2 delta = (currentMouse - mDragState.startMouse) * mDragState.sensitivity;
		mCameraState.angles = mDragState.startCameraState.angles + delta;
		// Clamp to avoid going too far when orbiting up/down
		mCameraState.angles.y = glm::clamp(mCameraState.angles.y, -glm::pi<float>() / 2 + 1e-5f, glm::pi<float>() / 2 - 1e-5f);
		updateViewMatrix();

		// Inertia
		mDragState.velocity = delta - mDragState.previousDelta;
		mDragState.previousDelta = delta;
	}
}

void Application::onMouseButton(int button, int action, int /*mods*/)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		switch (action) {
		case GLFW_PRESS:
			mDragState.active = true;
			double xpos, ypos;
			glfwGetCursorPos(mWindow, &xpos, &ypos);
			mDragState.startMouse = glm::vec2(-(float)xpos, (float)ypos);
			mDragState.startCameraState = mCameraState;
			break;
		case GLFW_RELEASE:
			mDragState.active = false;
			break;
		}
	}
}

void Application::onScroll(double /*xoffset*/, double yoffset)
{
	mCameraState.zoom += mDragState.scrollSensitivity * static_cast<float>(yoffset);
	mCameraState.zoom = glm::clamp(mCameraState.zoom, -2.0f, 2.0f);
	updateViewMatrix();
}

bool Application::initWindowAndDevice()
{

#ifdef __EMSCRIPTEN__
	Instance instance = createInstance();
#else
	InstanceDescriptor instanceDesc{};
	Instance instance = createInstance(instanceDesc);
#endif // ! __EMSCRIPTEN__

	// Check if the instance was created successfully
	if (!instance) {
		std::cerr << "Failed to create WebGPU instance. Could not initialize WebGPU" << std::endl;
		return false;
	}

	if (!glfwInit()) {
		std::cerr << "Could not initialize GLFW!" << std::endl;
		return false;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // NO_API bc We don't want OpenGL in the back, we'll use WebGPU instead.
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); 

	mWindow = glfwCreateWindow(mWindowWidth, mWindowHeight, "[WebGPU] 3D Playground", NULL, NULL);
	if (!mWindow) {
		std::cerr << "Could not open window!" << std::endl;
		return false;
	}

	// Capture surface here so we can use in the main loop
	mSurface = glfwGetWGPUSurface(instance, mWindow);

	std::cout << "Requesting adapter..." << std::endl;
	RequestAdapterOptions adapterOpts{};
	adapterOpts.compatibleSurface = mSurface;

	Adapter adapter = instance.requestAdapter(adapterOpts);
	std::cout << "Got adapter: " << adapter << std::endl;

	// It is good practice to release the instance as soon as we have the adapter.
	instance.release();

	std::cout << "Requesting device..." << std::endl;
	DeviceDescriptor deviceDesc{};
	deviceDesc.requiredFeatureCount = 0;
	deviceDesc.defaultQueue.nextInChain = nullptr;
	
	// A function that is invoked whenever the device stops being available.
	deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void* /* pUserData */) {
		std::cout << "Device lost: reason " << reason;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
		};

#ifdef __EMSCRIPTEN__
	deviceDesc.requiredLimits = nullptr;
#else
	RequiredLimits requiredLimits = getRequiredLimits(adapter);
	deviceDesc.requiredLimits = &requiredLimits;
#endif

	mDevice = adapter.requestDevice(deviceDesc);
	std::cout << "Got device: " << mDevice << std::endl;

	// A function that is invoked whenever there is an error in the use of the device
	mUncapturedErrorCallbackHandle = mDevice.setUncapturedErrorCallback([](ErrorType type, char const* message) {
		std::cout << "Uncaptured device error: type " << type;
		if (message) std::cout << " (" << message << ")";
		std::cout << std::endl;
		});

	mQueue = mDevice.getQueue();

#ifdef WEBGPU_BACKEND_WGPU
	mSurfaceFormat = mSurface.getPreferredFormat(adapter);
#else
	mSurfaceFormat = TextureFormat::BGRA8Unorm;
#endif

  // Store a pointer to the application in the GLFW window, so we can access it in callbacks if needed
  glfwSetWindowUserPointer(mWindow, this);
	

#ifndef __EMSCRIPTEN__
	glfwSetFramebufferSizeCallback(mWindow, [](GLFWwindow* window, int width, int height) {
		//std::cout << "[GLFW] framebuffer resize: " << width << " x " << height << std::endl;
		Application* that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
		if (that != nullptr) {
			that->handleResize(width, height);
		}
	});

	glfwSetCursorPosCallback(mWindow, [](GLFWwindow* window, double xpos, double ypos) {
		auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
		if (that != nullptr) that->onMouseMove(xpos, ypos);
	});

	glfwSetMouseButtonCallback(mWindow, [](GLFWwindow* window, int button, int action, int mods) {
		auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
		if (that != nullptr) that->onMouseButton(button, action, mods);
	});

	glfwSetScrollCallback(mWindow, [](GLFWwindow* window, double xoffset, double yoffset) {
		auto that = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
		if (that != nullptr) that->onScroll(xoffset, yoffset);
	});
#else
	emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, browserResizeCallback);

	emscripten_set_mousemove_callback("#canvas", this, true, mouseMoveCallback);

	emscripten_set_mousedown_callback("#canvas", this, true, mouseDownCallback);

	emscripten_set_mouseup_callback("#canvas", this, true, mouseUpCallback);

	emscripten_set_wheel_callback("#canvas", this, true, wheelCallback);

	//emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, keyDownCallback);

	//emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, keyUpCallback);
#endif

	adapter.release();
	return mDevice != nullptr;
}

void Application::terminateWindowAndDevice()
{
	mQueue.release();
	mDevice.release();
	mSurface.release();

	glfwDestroyWindow(mWindow);
	glfwTerminate();
}

RequiredLimits Application::getRequiredLimits(Adapter adapter)
{
	// Get adapter supported limits, in case we need them
	SupportedLimits supportedLimits;
	adapter.getLimits(&supportedLimits);

	// NOTE: is "Default" just an alias for a default initializer {} ? It crashes if I don't copy the supported limits manually.
	RequiredLimits requiredLimits = Default;
	requiredLimits.limits = supportedLimits.limits; // Start with the supported limits as a base, then override the ones we want to require

	requiredLimits.limits.maxVertexAttributes = 4;
	requiredLimits.limits.maxVertexBuffers = 1;
	requiredLimits.limits.maxBufferSize = 1500000 * sizeof(ResourceManager::VertexAttributes);
	requiredLimits.limits.maxVertexBufferArrayStride = sizeof(ResourceManager::VertexAttributes);
	requiredLimits.limits.maxInterStageShaderComponents = 8;
	requiredLimits.limits.maxBindGroups = 1;
	requiredLimits.limits.maxUniformBuffersPerShaderStage = 1;
	requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
	// For now allow textures up to 2k
	requiredLimits.limits.maxTextureDimension1D = 2048;
	requiredLimits.limits.maxTextureDimension2D = 2048;
	requiredLimits.limits.maxTextureArrayLayers = 1;
	requiredLimits.limits.maxSampledTexturesPerShaderStage = 1;
	requiredLimits.limits.maxSamplersPerShaderStage = 1;

	return requiredLimits;
}

void Application::configSurface()
{
	SurfaceConfiguration config{};
	config.width = static_cast<uint32_t>(mWindowWidth);
	config.height = static_cast<uint32_t>(mWindowHeight);
	config.usage = TextureUsage::RenderAttachment;
	config.format = mSurfaceFormat;
	config.viewFormatCount = 0;
	config.viewFormats = nullptr;
	config.device = mDevice;
	config.presentMode = PresentMode::Fifo;
	config.alphaMode = CompositeAlphaMode::Auto;

	mSurface.configure(config);
}

void Application::terminateSurfaceConfig()
{
  mSurface.unconfigure();
}

bool Application::initDepthBuffer()
{
	// Create the depth texture
	TextureDescriptor depthTextureDesc{};
	depthTextureDesc.dimension = TextureDimension::_2D;
	depthTextureDesc.format = mDepthTextureFormat;
	depthTextureDesc.mipLevelCount = 1;
	depthTextureDesc.sampleCount = 1;
	depthTextureDesc.size = { static_cast<uint32_t>(mWindowWidth),static_cast<uint32_t>(mWindowHeight), 1 };
	depthTextureDesc.usage = TextureUsage::RenderAttachment;
	depthTextureDesc.viewFormatCount = 1;
	depthTextureDesc.viewFormats = (WGPUTextureFormat*)&mDepthTextureFormat;
	mDepthTexture = mDevice.createTexture(depthTextureDesc);

	// Create the view of the depth texture manipulated by the rasterizer
	TextureViewDescriptor depthTextureViewDesc{};
	depthTextureViewDesc.aspect = TextureAspect::DepthOnly;
	depthTextureViewDesc.baseArrayLayer = 0;
	depthTextureViewDesc.arrayLayerCount = 1;
	depthTextureViewDesc.baseMipLevel = 0;
	depthTextureViewDesc.mipLevelCount = 1;
	depthTextureViewDesc.dimension = TextureViewDimension::_2D;
	depthTextureViewDesc.format = mDepthTextureFormat;
	mDepthTextureView = mDepthTexture.createView(depthTextureViewDesc);

	return mDepthTextureView != nullptr;
}

void Application::terminateDepthBuffer()
{
	mDepthTextureView.release();
	mDepthTexture.destroy();
	mDepthTexture.release();
}

bool Application::initRenderPipeline()
{
	std::cout << "Creating shader module..." << std::endl;
	mShaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/shader.wgsl", mDevice);

	// Check for errors
	if (mShaderModule == nullptr) {
		std::cerr << "Could not load shader!" << std::endl;
		exit(1);
	}
	else {
		std::cout << "Shader module: " << mShaderModule << std::endl;
	}

	RenderPipelineDescriptor pipelineDesc{};

	std::vector<VertexAttribute> vertexAttribs(4);

	// For each attrib, describe its layout, aka how to interpret the data
	// Position attribute
	vertexAttribs[0].shaderLocation = 0;
	vertexAttribs[0].format = VertexFormat::Float32x3;
	vertexAttribs[0].offset = offsetof(ResourceManager::VertexAttributes, position);

	// Normal attribute
	vertexAttribs[1].shaderLocation = 1;
	vertexAttribs[1].format = VertexFormat::Float32x3;
	vertexAttribs[1].offset = offsetof(ResourceManager::VertexAttributes, normal);

	// Color attribute
	vertexAttribs[2].shaderLocation = 2;
	vertexAttribs[2].format = VertexFormat::Float32x3;
	vertexAttribs[2].offset = offsetof(ResourceManager::VertexAttributes, color);

	// UV attribute
	vertexAttribs[3].shaderLocation = 3;
	vertexAttribs[3].format = VertexFormat::Float32x2;
	vertexAttribs[3].offset = offsetof(ResourceManager::VertexAttributes, uv);

	VertexBufferLayout vertexBufferLayout{};
	vertexBufferLayout.attributeCount = static_cast<uint32_t>(vertexAttribs.size());
	vertexBufferLayout.attributes = vertexAttribs.data();
	vertexBufferLayout.arrayStride = sizeof(ResourceManager::VertexAttributes);
	vertexBufferLayout.stepMode = VertexStepMode::Vertex; // We move to the next vertex for each vertex shader invocation

	pipelineDesc.vertex.bufferCount = 1;
	pipelineDesc.vertex.buffers = &vertexBufferLayout;
	pipelineDesc.vertex.module = mShaderModule;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = nullptr;

	// Each sequence of 3 vertices is considered as a triangle
	pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;
	// When not specified, vertices are considered sequentially.
	pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;

	pipelineDesc.primitive.frontFace = FrontFace::CCW;
	pipelineDesc.primitive.cullMode = CullMode::None;

	// We tell that the programmable fragment shader stage is described
	// by the function called 'fs_main' in the shader module.
	FragmentState fragmentState{};
	fragmentState.module = mShaderModule;
	fragmentState.entryPoint = "fs_main";
	fragmentState.constantCount = 0;
	fragmentState.constants = nullptr;

	BlendState blendState{};
	blendState.color.srcFactor = BlendFactor::SrcAlpha;
	blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
	blendState.color.operation = BlendOperation::Add;
	blendState.alpha.srcFactor = BlendFactor::Zero;
	blendState.alpha.dstFactor = BlendFactor::One;
	blendState.alpha.operation = BlendOperation::Add;

	ColorTargetState colorTarget{};
	colorTarget.format = mSurfaceFormat;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = ColorWriteMask::All; // We could write to only some of the color channels.

	// We have only one target because our render pass has only one output color attachment.
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;
	pipelineDesc.fragment = &fragmentState;

	// Setup depth state
	DepthStencilState depthStencilState = Default;
	depthStencilState.depthCompare = CompareFunction::Less;
	depthStencilState.depthWriteEnabled = true;
	depthStencilState.format = mDepthTextureFormat;
	depthStencilState.stencilReadMask = 0;
	depthStencilState.stencilWriteMask = 0;

	pipelineDesc.depthStencil = &depthStencilState;
	// Samples per pixel
	pipelineDesc.multisample.count = 1;
	// Default value for the mask, meaning "all bits on"
	pipelineDesc.multisample.mask = ~0u;
	// Default value as well (irrelevant for count = 1 anyways)
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	// Create a binding group
	std::vector<BindGroupLayoutEntry> bindingLayoutEntries(3, Default);

	BindGroupLayoutEntry& bindingLayout = bindingLayoutEntries[0];
	bindingLayout.binding = 0;
	bindingLayout.visibility = ShaderStage::Vertex | ShaderStage::Fragment;
	bindingLayout.buffer.type = BufferBindingType::Uniform;
	bindingLayout.buffer.minBindingSize = sizeof(BasicShaderUniforms);

	// The texture binding
	BindGroupLayoutEntry& textureBindingLayout = bindingLayoutEntries[1];
	textureBindingLayout.binding = 1;
	textureBindingLayout.visibility = ShaderStage::Fragment;
	textureBindingLayout.texture.sampleType = TextureSampleType::Float;
	textureBindingLayout.texture.viewDimension = TextureViewDimension::_2D;

	// The texture sampler binding
	BindGroupLayoutEntry& samplerBindingLayout = bindingLayoutEntries[2];
	samplerBindingLayout.binding = 2;
	samplerBindingLayout.visibility = ShaderStage::Fragment;
	samplerBindingLayout.sampler.type = SamplerBindingType::Filtering;

	// A bind group contains one or multiple bindings
	BindGroupLayoutDescriptor bindGroupLayoutDesc{};
	bindGroupLayoutDesc.entryCount = (uint32_t)bindingLayoutEntries.size();
	bindGroupLayoutDesc.entries = bindingLayoutEntries.data();
	mBindGroupLayout = mDevice.createBindGroupLayout(bindGroupLayoutDesc);

	// Create the pipeline layout
	PipelineLayoutDescriptor layoutDesc{};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&mBindGroupLayout;
	PipelineLayout layout = mDevice.createPipelineLayout(layoutDesc);

	pipelineDesc.layout = layout;

	mPipeline = mDevice.createRenderPipeline(pipelineDesc);

	return mPipeline != nullptr;
}

void Application::terminateRenderPipeline()
{
	mPipeline.release();
	mShaderModule.release();
	mBindGroupLayout.release();
}

bool Application::initTexture()
{
	SamplerDescriptor samplerDesc{};
	samplerDesc.addressModeU = AddressMode::Repeat;
	samplerDesc.addressModeV = AddressMode::Repeat;
	samplerDesc.addressModeW = AddressMode::Repeat;
	samplerDesc.magFilter = FilterMode::Linear;
	samplerDesc.minFilter = FilterMode::Linear;
	samplerDesc.mipmapFilter = MipmapFilterMode::Linear;
	samplerDesc.lodMinClamp = 0.0f;
	samplerDesc.lodMaxClamp = 8.0f;
	samplerDesc.compare = CompareFunction::Undefined;
	samplerDesc.maxAnisotropy = 1;
	mSampler = mDevice.createSampler(samplerDesc);

	mTexture = ResourceManager::loadTexture(RESOURCE_DIR "/fourareen2K_albedo.jpg", mDevice, &mTextureView);
	if (!mTexture) {
		std::cerr << "Could not load texture!" << std::endl;
		return false;
	}
  return mTextureView != nullptr;
}

void Application::terminateTexture()
{
	mTextureView.release();
	mTexture.destroy();
	mTexture.release();
	mSampler.release();
}

TextureView Application::getNextSurfaceTextureView()
{
	// Get the surface Texture
	SurfaceTexture surfaceTexture{};
	mSurface.getCurrentTexture(&surfaceTexture);
	if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success) {
		return nullptr;
	}
	Texture texture = surfaceTexture.texture;

	// Create a view for this surface texture
	TextureViewDescriptor viewDescriptor{};
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
	texture.release();
#endif // WEBGPU_BACKEND_WGPU

	return targetView;
}

bool Application::initGeometry()
{
	// Load mesh data from OBJ file
	std::vector<ResourceManager::VertexAttributes> vertexData;
	bool success = ResourceManager::loadGeometryFromObj(RESOURCE_DIR "/fourareen.obj", vertexData);
	if (!success) {
		std::cerr << "Could not load geometry!" << std::endl;
		return false;
	}

	// Create vertex buffer
	BufferDescriptor bufferDesc{};
	bufferDesc.size = vertexData.size() * sizeof(ResourceManager::VertexAttributes);
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Vertex;
	bufferDesc.mappedAtCreation = false;
	mVertexBuffer = mDevice.createBuffer(bufferDesc);
	mQueue.writeBuffer(mVertexBuffer, 0, vertexData.data(), bufferDesc.size);

	mVertexCount = static_cast<int>(vertexData.size());

	return mVertexBuffer != nullptr;
}

void Application::terminateGeometry()
{
	mVertexBuffer.destroy();
	mVertexBuffer.release();
	mVertexCount = 0;
}

bool Application::initUniforms()
{
	// Create uniform buffer
	BufferDescriptor bufferDesc{};
	bufferDesc.size = sizeof(BasicShaderUniforms);
	bufferDesc.usage = BufferUsage::CopyDst | BufferUsage::Uniform;
	bufferDesc.mappedAtCreation = false;
	mUniformBuffer = mDevice.createBuffer(bufferDesc);

	// Upload the initial value of the uniforms
	mUniforms.modelMatrix = glm::mat4(1.0f);
	//mUniforms.viewMatrix = glm::lookAt(glm::vec3(-2.0f, -3.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  updateViewMatrix();
	//mUniforms.projectionMatrix = glm::perspective(45 * glm::pi<float>() / 180.0f, 640.0f / 480.0f, 0.01f, 100.0f);
  updateProjectionMatrix();
	mUniforms.time = 1.0f;
	mUniforms.color = { 0.0f, 1.0f, 0.4f, 1.0f };
	mQueue.writeBuffer(mUniformBuffer, 0, &mUniforms, sizeof(BasicShaderUniforms));

	return mUniformBuffer != nullptr;
}

void Application::terminateUniforms()
{
	mUniformBuffer.destroy();
	mUniformBuffer.release();
}

bool Application::initBindGroup()
{
	// Create a binding
	std::vector<BindGroupEntry> bindings(3);
	bindings[0].binding = 0;
	bindings[0].buffer = mUniformBuffer;
	bindings[0].offset = 0;
	bindings[0].size = sizeof(BasicShaderUniforms);

	bindings[1].binding = 1;
	bindings[1].textureView = mTextureView;

	bindings[2].binding = 2;
	bindings[2].sampler = mSampler;

	BindGroupDescriptor bindGroupDesc{};
	bindGroupDesc.layout = mBindGroupLayout;
	bindGroupDesc.entryCount = (uint32_t)bindings.size();
	bindGroupDesc.entries = bindings.data();
	mBindGroup = mDevice.createBindGroup(bindGroupDesc);

  return mBindGroup != nullptr;
}

void Application::terminateBindGroup()
{
  mBindGroup.release();
}

void Application::handleResize(int width, int height)
{
	mWindowWidth = width;
	mWindowHeight = height;
  onResize();
}


#ifdef __EMSCRIPTEN__
EM_BOOL Application::browserResizeCallback(int /*eventType*/, const EmscriptenUiEvent* event, void* userData) {
	int width = event->windowInnerWidth;
	int height = event->windowInnerHeight;

	//std::cout << "[Browser] window resize: " << width << " x " << height << std::endl;

	emscripten_set_canvas_element_size("#canvas", width, height);

	Application* app = reinterpret_cast<Application*>(userData);
  app->handleResize(width, height);

	return EM_TRUE;
}

EM_BOOL Application::mouseMoveCallback(int /*eventType*/, const EmscriptenMouseEvent* e, void* userData) {
	Application* app = reinterpret_cast<Application*>(userData);

	double x = e->targetX;
	double y = e->targetY;

	app->onMouseMove(x, y);

	return EM_TRUE;
}

EM_BOOL Application::mouseDownCallback(int /*eventType*/, const EmscriptenMouseEvent* e, void* userData) {
	Application* app = reinterpret_cast<Application*>(userData);

	int button = e->button;

	app->onMouseButton(button, GLFW_PRESS, 0);

	return EM_TRUE;
}

EM_BOOL Application::mouseUpCallback(int /*eventType*/, const EmscriptenMouseEvent* e, void* userData) {
	Application* app = reinterpret_cast<Application*>(userData);

	int button = e->button;

	app->onMouseButton(button, GLFW_RELEASE, 0);

	return EM_TRUE;
}

EM_BOOL Application::wheelCallback(int /*eventType*/ , const EmscriptenWheelEvent* e, void* userData) {
	Application* app = reinterpret_cast<Application*>(userData);

	double normalized = -e->deltaY * 0.01;
	app->onScroll(0.0, normalized);

	return EM_TRUE;
}
#endif

void Application::updateViewMatrix()
{
	float cx = cos(mCameraState.angles.x);
	float sx = sin(mCameraState.angles.x);
	float cy = cos(mCameraState.angles.y);
	float sy = sin(mCameraState.angles.y);
	glm::vec3 position = glm::vec3(cx * cy, sx * cy, sy) * std::exp(-mCameraState.zoom);
	mUniforms.viewMatrix = glm::lookAt(position, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	mQueue.writeBuffer(
		mUniformBuffer,
		offsetof(BasicShaderUniforms, viewMatrix),
		&mUniforms.viewMatrix,
		sizeof(BasicShaderUniforms::viewMatrix)
	);
}

void Application::updateProjectionMatrix()
{
	float ratio = mWindowWidth / (float)mWindowHeight;
	mUniforms.projectionMatrix = glm::perspective(45 * glm::pi<float>() / 180.0f, ratio, 0.01f, 100.0f);
	mQueue.writeBuffer(
		mUniformBuffer,
		offsetof(BasicShaderUniforms, projectionMatrix),
		&mUniforms.projectionMatrix,
		sizeof(BasicShaderUniforms::projectionMatrix)
	);
}

void Application::updateDragInertia()
{
	constexpr float eps = 1e-4f;
	// Apply inertia only when the user released the click.
	if (!mDragState.active) {
		// Avoid updating the matrix when the velocity is no longer noticeable
		if (std::abs(mDragState.velocity.x) < eps && std::abs(mDragState.velocity.y) < eps) {
			return;
		}
		mCameraState.angles += mDragState.velocity;
		mCameraState.angles.y = glm::clamp(mCameraState.angles.y, -glm::pi<float>() / 2 + 1e-5f, glm::pi<float>() / 2 - 1e-5f);
		// Dampen the velocity so that it decreases exponentially and stops
		// after a few frames.
		mDragState.velocity *= mDragState.inertia;
		updateViewMatrix();
	}
}
