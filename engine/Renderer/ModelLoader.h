#pragma once
#include <tiny_gltf.h>
#include <vector>
#include <glm/glm.hpp>
#include "BufferUtils.h"
#include "SceneObject.h"
#include "Material.h"

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

bool loadGLTFModel(const std::string &path, Mesh &outMesh);

SceneObject loadGLTFModelToSceneObject(const std::string &path, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout);

Material *createMaterialFromGLTFImage(const tinygltf::Image &gltfImage, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout);