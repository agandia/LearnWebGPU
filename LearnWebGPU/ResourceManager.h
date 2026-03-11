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

		// Texture mapping attributes represent the local frame in which
		// normals sampled from the normal map are expressed.
		glm::vec3 tangent; // T = local X axis
		glm::vec3 bitangent; // B = local Y axis
		glm::vec3 normal; // N = local Z axis

		glm::vec3 color;
    glm::vec2 uv;
	};

	
	// Create a shader module for a given WebGPU `device` from a WGSL shader source loaded from a path
	static wgpu::ShaderModule loadShaderModule(const std::filesystem::path& path, wgpu::Device device);
	
	// Load an 3D mesh from a standard .obj file into a vertex data buffer
	static bool loadGeometryFromObj(const std::filesystem::path& path, std::vector<VertexAttributes>& vertexData);

	// Load an image from a standard image file into a new texture object
	static wgpu::Texture loadTexture(const std::filesystem::path& path, wgpu::Device m_device, wgpu::TextureView* pTextureView = nullptr);

private:
	// Compute the TBN local to a triangle face from its corners and return it as
	// a matrix whose columns are the T, B and N vectors.
	static glm::mat3 computeTBN(const VertexAttributes corners[3], const glm::vec3& expectedN);
	// Compute Tangent and Bitangent attributes from the normal and UVs.
	static void populateTextureFrameAttributes(std::vector<VertexAttributes>& vertexData);
};