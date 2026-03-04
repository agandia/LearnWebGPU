#pragma once

#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>

 // Forward declare
struct GLFWwindow;

class Application {
public:
	// Init State or Return with fail message
	bool onInit();

	// Interval updates
	void onFrame();

  // Free resources and terminate the application
	void onFinish();

	// keep alive check
	bool isRunning();

private:
	bool initWindowAndDevice();
	void terminateWindowAndDevice();

  wgpu::RequiredLimits getRequiredLimits(wgpu::Adapter adapter);

	void configSurface();
	void terminateSurfaceConfig();

	bool initDepthBuffer();
	void terminateDepthBuffer();

	bool initRenderPipeline();
	void terminateRenderPipeline();

	bool initTexture();
	void terminateTexture();

	wgpu::TextureView getNextSurfaceTextureView();

	bool initGeometry();
	void terminateGeometry();

	bool initUniforms();
	void terminateUniforms();

	bool initBindGroup();
	void terminateBindGroup();

private:

	/**
	 * The same structure as in the shader, replicated in C++
	 */
	struct BasicShaderUniforms {
		// We add transform matrices
		glm::mat4 projectionMatrix;
		glm::mat4 viewMatrix;
		glm::mat4 modelMatrix;
		glm::vec4 color;
		float time;
		float _pad[3];
	};
	// Have the compiler check byte alignment
	static_assert(sizeof(BasicShaderUniforms) % 16 == 0);

	// Window and Device
	GLFWwindow* mWindow = nullptr;
	wgpu::Surface mSurface = nullptr;
	wgpu::Device mDevice = nullptr;
	wgpu::Queue mQueue = nullptr;
	wgpu::TextureFormat mSurfaceFormat = wgpu::TextureFormat::Undefined;
	// Keep the error callback alive
	std::unique_ptr<wgpu::ErrorCallback> mUncapturedErrorCallbackHandle;

  // Surface configuration
	wgpu::SurfaceConfiguration mSurfaceConfig = {};

	// Depth Buffer
	wgpu::TextureFormat mDepthTextureFormat = wgpu::TextureFormat::Depth24Plus;
	wgpu::Texture mDepthTexture = nullptr;
	wgpu::TextureView mDepthTextureView = nullptr;

	// Render Pipeline
	wgpu::BindGroupLayout mBindGroupLayout = nullptr;
	wgpu::ShaderModule mShaderModule = nullptr;
	wgpu::RenderPipeline mPipeline = nullptr;

	// Texture
	wgpu::Sampler mSampler = nullptr;
	wgpu::Texture mTexture = nullptr;
	wgpu::TextureView mTextureView = nullptr;

	// Geometry
	wgpu::Buffer mVertexBuffer = nullptr;
	int mVertexCount = 0;

	// Uniforms
	wgpu::Buffer mUniformBuffer = nullptr;
	BasicShaderUniforms mUniforms;

	// Bind Group
	wgpu::BindGroup mBindGroup = nullptr;
};