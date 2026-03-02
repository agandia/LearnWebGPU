// In ResourceManager.cpp
#include "ResourceManager.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace wgpu;

bool ResourceManager::loadGeometry(
	const std::filesystem::path& path,
	std::vector<float>& pointData,
	std::vector<uint16_t>& indexData,
	int dimensions
) {
	std::ifstream file(path);
	if (!file.is_open()) {
		return false;
	}

	pointData.clear();
	indexData.clear();

	enum class Section {
		None,
		Points,
		Indices,
	};
	Section currentSection = Section::None;

	float value;
	uint16_t index;
	std::string line;
	while (!file.eof()) {
		getline(file, line);

		// overcome the `CRLF` problem
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line == "[points]") {
			currentSection = Section::Points;
		}
		else if (line == "[indices]") {
			currentSection = Section::Indices;
		}
		else if (line[0] == '#' || line.empty()) {
			// Do nothing, this is a comment
		}
		else if (currentSection == Section::Points) {
			std::istringstream iss(line);
			// Get x, y, z, r, g, b
			for (int i = 0; i < dimensions + 3; ++i) {
				iss >> value;
				pointData.push_back(value);
			}
		}
		else if (currentSection == Section::Indices) {
			std::istringstream iss(line);
			// Get corners #0 #1 and #2
			for (int i = 0; i < 3; ++i) {
				iss >> index;
				indexData.push_back(index);
			}
		}
	}
	return true;
}

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