#pragma once

#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif // __EMSCRIPTEN__

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

	// Window events
	void onResize();

	// Mouse events
	void onMouseMove(double xpos, double ypos);
	void onMouseButton(int button, int action, int mods);
	void onScroll(double xoffset, double yoffset);


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
	
  void handleResize(int width, int height);
#ifdef __EMSCRIPTEN__
	static EM_BOOL browserResizeCallback(int eventType, const EmscriptenUiEvent* event, void* userData);

	static EM_BOOL mouseMoveCallback(int eventType, const EmscriptenMouseEvent* e, void* userData);

	static EM_BOOL mouseDownCallback(int eventType, const EmscriptenMouseEvent* e, void* userData);

	static EM_BOOL mouseUpCallback(int eventType, const EmscriptenMouseEvent* e, void* userData);

	static EM_BOOL wheelCallback(int eventType, const EmscriptenWheelEvent* e, void* userData);
#endif
  void updateViewMatrix();
	void updateProjectionMatrix();

  void updateDragInertia();

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

	struct CameraState {
		// angles.x is the rotation of the camera around the global vertical axis, affected by mouse.x
		// angles.y is the rotation of the camera around its local horizontal axis, affected by mouse.y
		glm::vec2 angles = { 0.8f, 0.5f };
		// zoom is the position of the camera along its local forward axis, affected by the scroll wheel
		float zoom = -1.2f;
	};

	struct DragState {
		// Whether a drag action is ongoing (i.e., we are between mouse press and mouse release)
		bool active = false;
		// The position of the mouse at the beginning of the drag action
		glm::vec2 startMouse;
		// The camera state at the beginning of the drag action
		CameraState startCameraState;

		// Constant settings
		float sensitivity = 0.01f;
		float scrollSensitivity = 0.1f;

		// Inertia
		glm::vec2 velocity = { 0.0f, 0.0f };
		glm::vec2 previousDelta;
		float inertia = 0.9f;
	};


	// Window and Device
	GLFWwindow* mWindow = nullptr;
  uint32_t mWindowWidth = 1920;
  uint32_t mWindowHeight = 1080;

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

  CameraState mCameraState;
  DragState mDragState;
};