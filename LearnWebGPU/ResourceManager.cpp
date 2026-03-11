#include "ResourceManager.h"

#include <fstream>
#include <string>

#include "tiny_obj_loader.h"
#include "stb_image.h"

using namespace wgpu;

ShaderModule ResourceManager::loadShaderModule(const std::filesystem::path& path, Device device) {
    // Open the file in binary mode to preserve line endings exactly
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Could not load shader: " << path << std::endl;
        return nullptr;
    }

    // Read the entire file into a string without modifying line endings
    std::string shaderSource((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    // Guarantee null-termination for WGSL descriptor
    shaderSource.push_back('\0');

    ShaderModuleWGSLDescriptor shaderCodeDesc{};
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    shaderCodeDesc.code = shaderSource.c_str();

    ShaderModuleDescriptor shaderDesc{};
#ifdef WEBGPU_BACKEND_WGPU
    shaderDesc.hintCount = 0;
    shaderDesc.hints = nullptr;
#endif
    shaderDesc.nextInChain = &shaderCodeDesc.chain;

    return device.createShaderModule(shaderDesc);
}

bool ResourceManager::loadGeometryFromObj(const std::filesystem::path& path, std::vector<VertexAttributes>& vertexData) {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	std::string warn;
	std::string err;

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str());

	if (!warn.empty()) {
		std::cout << warn << std::endl;
	}

	if (!err.empty()) {
		std::cerr << err << std::endl;
	}

	if (!ret) {
		return false;
	}

	// Filling in vertexData:
	vertexData.clear();
	for (const auto& shape : shapes) {
		size_t offset = vertexData.size();
		vertexData.resize(offset + shape.mesh.indices.size());

		for (size_t i = 0; i < shape.mesh.indices.size(); ++i) {
			const tinyobj::index_t& idx = shape.mesh.indices[i];

			vertexData[offset + i].position = {
				attrib.vertices[3 * idx.vertex_index + 0],
				-attrib.vertices[3 * idx.vertex_index + 2],
				attrib.vertices[3 * idx.vertex_index + 1]
			};

			vertexData[offset + i].normal = {
				attrib.normals[3 * idx.normal_index + 0],
				-attrib.normals[3 * idx.normal_index + 2],
				attrib.normals[3 * idx.normal_index + 1]
			};

			vertexData[offset + i].color = {
				attrib.colors[3 * idx.vertex_index + 0],
				attrib.colors[3 * idx.vertex_index + 1],
				attrib.colors[3 * idx.vertex_index + 2]
			}; 
			
			vertexData[offset + i].uv = {
				attrib.texcoords[2 * idx.texcoord_index + 0],
				1 - attrib.texcoords[2 * idx.texcoord_index + 1]
			};
		}
	}

	populateTextureFrameAttributes(vertexData);

	return true;
}

// Auxiliary function for loadTexture
static void writeMipMaps(Device device, Texture m_texture, Extent3D textureSize, uint32_t mipLevelCount, const unsigned char* pixelData) {
	Queue queue = device.getQueue();

	// Arguments telling which part of the texture we upload to
	ImageCopyTexture destination{};
	destination.texture = m_texture;
	destination.origin = { 0, 0, 0 };
	destination.aspect = TextureAspect::All;

	// Arguments telling how the C++ side pixel memory is laid out
	TextureDataLayout source{};
	source.offset = 0;

	// Create image data
	Extent3D mipLevelSize = textureSize;
	std::vector<unsigned char> previousLevelPixels;
	Extent3D previousMipLevelSize{};
	for (uint32_t level = 0; level < mipLevelCount; ++level) {
		// Pixel data for the current level
		std::vector<unsigned char> pixels(4 * mipLevelSize.width * mipLevelSize.height);
		if (level == 0) {
			// We cannot really avoid this copy since we need this
			// in previousLevelPixels at the next iteration
			memcpy(pixels.data(), pixelData, pixels.size());
		}
		else {
			// Create mip level data
			for (uint32_t i = 0; i < mipLevelSize.width; ++i) {
				for (uint32_t j = 0; j < mipLevelSize.height; ++j) {
					unsigned char* p = &pixels[4 * (j * mipLevelSize.width + i)];
					// Get the corresponding 4 pixels from the previous level
					unsigned char* p00 = &previousLevelPixels[4 * ((2 * j + 0) * previousMipLevelSize.width + (2 * i + 0))];
					unsigned char* p01 = &previousLevelPixels[4 * ((2 * j + 0) * previousMipLevelSize.width + (2 * i + 1))];
					unsigned char* p10 = &previousLevelPixels[4 * ((2 * j + 1) * previousMipLevelSize.width + (2 * i + 0))];
					unsigned char* p11 = &previousLevelPixels[4 * ((2 * j + 1) * previousMipLevelSize.width + (2 * i + 1))];
					// Average
					p[0] = (p00[0] + p01[0] + p10[0] + p11[0]) / 4;
					p[1] = (p00[1] + p01[1] + p10[1] + p11[1]) / 4;
					p[2] = (p00[2] + p01[2] + p10[2] + p11[2]) / 4;
					p[3] = (p00[3] + p01[3] + p10[3] + p11[3]) / 4;
				}
			}
		}

		// Upload data to the GPU texture
		destination.mipLevel = level;
		source.bytesPerRow = 4 * mipLevelSize.width;
		source.rowsPerImage = mipLevelSize.height;
		queue.writeTexture(destination, pixels.data(), pixels.size(), source, mipLevelSize);

		previousLevelPixels = std::move(pixels);
		previousMipLevelSize = mipLevelSize;
		mipLevelSize.width /= 2;
		mipLevelSize.height /= 2;
	}

	queue.release();
}


Texture ResourceManager::loadTexture(const std::filesystem::path& path, Device device, TextureView* pTextureView) {
	int width, height, channels;
	unsigned char* pixelData = stbi_load(path.string().c_str(), &width, &height, &channels, 4 /* force 4 channels */);
	
	// If data is null, loading failed.
	if (!pixelData) {
		std::cerr << "Failed to load texture: " << path << std::endl;
		return nullptr;
	}

	// Use the width, height, channels and data variables here
	TextureDescriptor textureDesc{};
	textureDesc.dimension = TextureDimension::_2D;
	textureDesc.format = TextureFormat::RGBA8Unorm; // by convention for bmp, png and jpg file. Be careful with other formats.
	textureDesc.size = { (unsigned int)width, (unsigned int)height, 1 };
	textureDesc.mipLevelCount = std::bit_width(std::max(textureDesc.size.width, textureDesc.size.height));
	textureDesc.sampleCount = 1;
	textureDesc.usage = TextureUsage::TextureBinding | TextureUsage::CopyDst;
	textureDesc.viewFormatCount = 0;
	textureDesc.viewFormats = nullptr;
	Texture m_texture = device.createTexture(textureDesc);

	// Upload data to the GPU texture
	writeMipMaps(device, m_texture, textureDesc.size, textureDesc.mipLevelCount, pixelData);

	stbi_image_free(pixelData);
	// (Do not use data after this)

	if (pTextureView) {
		TextureViewDescriptor textureViewDesc{};
		textureViewDesc.aspect = TextureAspect::All;
		textureViewDesc.baseArrayLayer = 0;
		textureViewDesc.arrayLayerCount = 1;
		textureViewDesc.baseMipLevel = 0;
		textureViewDesc.mipLevelCount = textureDesc.mipLevelCount;
		textureViewDesc.dimension = TextureViewDimension::_2D;
		textureViewDesc.format = textureDesc.format;
		*pTextureView = m_texture.createView(textureViewDesc);
	}

	return m_texture;
}

glm::mat3 ResourceManager::computeTBN(const VertexAttributes corners[3], const glm::vec3& expectedN) {
	glm::vec3 ePos1 = corners[1].position - corners[0].position;
	glm::vec3 ePos2 = corners[2].position - corners[0].position;

	// What we call \bar e in the figure
	glm::vec2 eUV1 = corners[1].uv - corners[0].uv;
	glm::vec2 eUV2 = corners[2].uv - corners[0].uv;

	glm::vec3 T = normalize(ePos1 * eUV2.y - ePos2 * eUV1.y);
	glm::vec3 B = normalize(ePos2 * eUV1.x - ePos1 * eUV2.x);
	glm::vec3 N = cross(T, B);

	// Fix overall orientation
	if (dot(N, expectedN) < 0.0) {
		T = -T;
		B = -B;
		N = -N;
	}

	// Ortho-normalize the (T, B, expectedN) frame
	// a. "Remove" the part of T that is along expected N
	N = expectedN;
	T = normalize(T - dot(T, N) * N);
	// b. Recompute B from N and T
	B = cross(N, T);

	return glm::mat3(T, B, N);
}

void ResourceManager::populateTextureFrameAttributes(std::vector<VertexAttributes>& vertexData) {
	size_t triangleCount = vertexData.size() / 3;
	// We compute the local texture frame per triangle
	for (size_t t = 0; t < triangleCount; ++t) {
		VertexAttributes* v = &vertexData[3 * t];

		for (int k = 0; k < 3; ++k) {
			glm::mat3 TBN = computeTBN(v, v[k].normal);
			v[k].tangent = TBN[0];
			v[k].bitangent = TBN[1];
		}
	}
}
