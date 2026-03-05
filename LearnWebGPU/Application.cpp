#include "Application.h"
#include "ResourceManager.h"

#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif // __EMSCRIPTEN__

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

	float viewZ = glm::mix(0.0f, 0.25f, glm::cos(2 * glm::pi<float>() * time / 4.0f) * 0.5f + 0.5f);
	mUniforms.viewMatrix = glm::lookAt(glm::vec3(-0.5f, -1.5f, viewZ + 0.25f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	mQueue.writeBuffer(mUniformBuffer, offsetof(BasicShaderUniforms, viewMatrix), &mUniforms.viewMatrix, sizeof(BasicShaderUniforms::viewMatrix));
	
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
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // No resizing for now

	mWindow = glfwCreateWindow(640, 480, "[WebGPU] 3D Playground", NULL, NULL);
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
	config.width = 640;
	config.height = 480;
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
	depthTextureDesc.size = { 640, 480, 1 };
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
	mUniforms.viewMatrix = glm::lookAt(glm::vec3(-2.0f, -3.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	mUniforms.projectionMatrix = glm::perspective(45 * glm::pi<float>() / 180.0f, 640.0f / 480.0f, 0.01f, 100.0f);
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
