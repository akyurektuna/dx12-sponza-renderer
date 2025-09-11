#include "OBJLoader.h"
#include "tiny_obj_loader.h"
#include <debugapi.h>
#include <unordered_map>
#include <sstream>
#include <iostream>

namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(const Vertex& v) const {
            size_t seed = 0;

            // hash position
            hash<float> float_hasher;
            seed ^= float_hasher(v.position.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= float_hasher(v.position.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= float_hasher(v.position.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

            // Hash normal
            seed ^= float_hasher(v.normal.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= float_hasher(v.normal.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= float_hasher(v.normal.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

            // Hash texCoord
            seed ^= float_hasher(v.texCoord.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= float_hasher(v.texCoord.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

            return seed;
        }
    };
}

bool operator==(const Vertex& a, const Vertex& b) {
    return a.position.x == b.position.x &&
        a.position.y == b.position.y &&
        a.position.z == b.position.z &&
        a.normal.x == b.normal.x &&
        a.normal.y == b.normal.y &&
        a.normal.z == b.normal.z &&
        a.texCoord.x == b.texCoord.x &&
        a.texCoord.y == b.texCoord.y;
}

bool OBJLoader::LoadOBJ(const std::string& filename, std::vector<Mesh>& meshes, std::string& error)
{
    OutputDebugStringA("************** OBJLoader started **************\n");

    tinyobj::ObjReaderConfig reader_config;
    reader_config.mtl_search_path = "C:\\Users\\akyur\\Documents\\graphics-github\\dx12-sponza-renderer\\dx12-sponza-renderer\\models\\"; // path to MTL files

    reader_config.triangulate = true;

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(filename, reader_config))
    {
        error = reader.Error();
        OutputDebugStringA(("ERROR: " + error + "\n").c_str());
        return false;
    }

    if (!reader.Warning().empty())
    {
        OutputDebugStringA(("WARNING: " + reader.Warning() + "\n").c_str());
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& materials = reader.GetMaterials();

    std::stringstream debugMsg;
    debugMsg << "loaded: " << shapes.size() << " shapes, "
        << materials.size() << " materials, "
        << attrib.vertices.size() / 3 << " vertices\n";
    OutputDebugStringA(debugMsg.str().c_str());

    for (const auto& shape : shapes)
    {
        Mesh mesh;
        std::unordered_map<Vertex, uint32_t> uniqueVertices;
        size_t index_offset = 0;

        // assign material name if available
        if (!shape.mesh.material_ids.empty() && shape.mesh.material_ids[0] >= 0) {
            int material_id = shape.mesh.material_ids[0];
            if (material_id < materials.size()) {
                mesh.materialName = materials[material_id].name;
            }
        }

        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
        {
            size_t fv = size_t(shape.mesh.num_face_vertices[f]);

            // ensure it's a triangle
            if (fv != 3) {
                OutputDebugStringA("warning: Non-triangular face found, skipping\n");
                index_offset += fv;
                continue;
            }

            for (size_t v = 0; v < fv; v++)
            {
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                Vertex vertex;

                // position (required)
                vertex.position = {
                    attrib.vertices[3 * size_t(idx.vertex_index) + 0],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 1],
                    attrib.vertices[3 * size_t(idx.vertex_index) + 2]
                };

                // normal (optional)
                if (idx.normal_index >= 0 && !attrib.normals.empty())
                {
                    vertex.normal = {
                        attrib.normals[3 * size_t(idx.normal_index) + 0],
                        attrib.normals[3 * size_t(idx.normal_index) + 1],
                        attrib.normals[3 * size_t(idx.normal_index) + 2]
                    };
                }
                else
                {
                    vertex.normal = { 0.0f, 1.0f, 0.0f }; // default normal (up)
                }

                // texCoord (optional)
                if (idx.texcoord_index >= 0 && !attrib.texcoords.empty())
                {
                    vertex.texCoord = {
                        attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
                        1.0f - attrib.texcoords[2 * size_t(idx.texcoord_index) + 1] // Flip V
                    };
                }
                else
                {
                    vertex.texCoord = { 0.0f, 0.0f }; // default texcoord
                }

                // check for duplicate vertices
                if (uniqueVertices.count(vertex) == 0)
                {
                    uniqueVertices[vertex] = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(vertex);
                }

                mesh.indices.push_back(uniqueVertices[vertex]);
            }
            index_offset += fv;
        }

        // only add the mesh if it has vertices
        if (!mesh.vertices.empty()) {
            meshes.push_back(mesh);
        }
    }

    OutputDebugStringA("************** OBJLoader completed **************\n");
    return true;
}