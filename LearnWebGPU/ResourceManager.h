#pragma once

#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>

#include <vector>
#include <filesystem>

class ResourceManager {
public:

	/**
	* A structure that describes the data layout in the vertex buffer
	*/
	struct VertexAttributes {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
    glm::vec2 uv;
	};

	
	// Create a shader module for a given WebGPU `device` from a WGSL shader source loaded from a path
	static wgpu::ShaderModule loadShaderModule(const std::filesystem::path& path, wgpu::Device device);
	
	// Load an 3D mesh from a standard .obj file into a vertex data buffer
	static bool loadGeometryFromObj(const std::filesystem::path& path, std::vector<VertexAttributes>& vertexData);

	// Load an image from a standard image file into a new texture object
	static wgpu::Texture loadTexture(const std::filesystem::path& path, wgpu::Device m_device, wgpu::TextureView* pTextureView = nullptr);
};