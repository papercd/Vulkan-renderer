#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "ModelLoader.h"
#include <glm/gtc/type_ptr.hpp> // for glm::make_vec3 / make_vec2
#include <iostream>

bool loadGLTFModel(const std::string &path, Mesh &outMesh)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool success = loader.LoadBinaryFromFile(&model, &err, &warn, path); // for .glb
    if (!success)
    {
        std::cerr << "Failed to load glTF: " << err << "\n";
        std::cerr << "Attempted path: " << path << "\n";

        return false;
    }

    if (!warn.empty())
        std::cout << "glTF warning: " << warn << "\n";

    // Assume one mesh for now
    if (model.meshes.empty())
        return false;
    const auto &mesh = model.meshes[0];

    for (const auto &primitive : mesh.primitives)
    {
        if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
            continue;

        const auto &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
        const auto &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
        const auto &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];

        const auto &posView = model.bufferViews[posAccessor.bufferView];
        const auto &normView = model.bufferViews[normAccessor.bufferView];
        const auto &uvView = model.bufferViews[uvAccessor.bufferView];

        const auto &buffer = model.buffers[0]; // Assume 1 buffer

        const float *posData = reinterpret_cast<const float *>(&buffer.data[posView.byteOffset + posAccessor.byteOffset]);
        const float *normData = reinterpret_cast<const float *>(&buffer.data[normView.byteOffset + normAccessor.byteOffset]);
        const float *uvData = reinterpret_cast<const float *>(&buffer.data[uvView.byteOffset + uvAccessor.byteOffset]);

        outMesh.vertices.resize(posAccessor.count);
        for (size_t i = 0; i < posAccessor.count; ++i)
        {
            outMesh.vertices[i].pos = glm::make_vec3(&posData[i * 3]);
            outMesh.vertices[i].normal = glm::make_vec3(&normData[i * 3]);
            outMesh.vertices[i].texCoord = glm::make_vec2(&uvData[i * 2]);
        }

        const auto &idxAccessor = model.accessors[primitive.indices];
        const auto &idxView = model.bufferViews[idxAccessor.bufferView];
        const void *indexData = &buffer.data[idxView.byteOffset + idxAccessor.byteOffset];

        outMesh.indices.resize(idxAccessor.count);
        if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
        {
            const uint16_t *idx = static_cast<const uint16_t *>(indexData);
            for (size_t i = 0; i < idxAccessor.count; ++i)
                outMesh.indices[i] = static_cast<uint32_t>(idx[i]);
        }
        else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
        {
            const uint32_t *idx = static_cast<const uint32_t *>(indexData);
            for (size_t i = 0; i < idxAccessor.count; ++i)
                outMesh.indices[i] = idx[i];
        }
        else
        {
            std::cerr << "Unsupported index type.\n";
            return false;
        }
    }

    return true;
}
