#pragma once

#include <webgpu/webgpu.hpp>
#include <glm/glm.hpp>

#include <vector>
#include <filesystem>

class ResourceManager {
public:

	/**
	* A structure that describes the data layout in the vertex buffer
	* We do not instantiate it but use it in `sizeof` and `offsetof`
	*/
	struct VertexAttributes {
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec3 color;
	};

	/**
	 * Load a file from `path` using our ad-hoc format and populate the `pointData`
	 * and `indexData` vectors.
	 */
	static bool loadGeometry(const std::filesystem::path& path, std::vector<float>& pointData, std::vector<uint16_t>& indexData, int dimensions = 2);

	/**
	 * Create a shader module for a given WebGPU `device` from a WGSL shader source
	 * loaded from file `path`.
	 */
	static wgpu::ShaderModule loadShaderModule(const std::filesystem::path& path, wgpu::Device device);
	
	// Load an 3D mesh from a standard .obj file into a vertex data buffer
	static bool loadGeometryFromObj(const std::filesystem::path& path, std::vector<VertexAttributes>& vertexData);
};