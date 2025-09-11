#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include <DirectxMath.h>


struct Vertex 
{
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT2 texCoord;
};

struct Mesh 
{
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::string materialName;
};

class OBJLoader 
{
public:
	static bool LoadOBJ(
		const std::string& filename,
		std::vector<Mesh>& meshes,
		std::string& error);
};