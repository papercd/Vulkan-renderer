#pragma once
#include <vulkan/vulkan.h>

struct alignas(16) MaterialParams {
    glm::vec4 emissiveFactor_andAO; // xyz = emissiveFactor, w = occlusionStrength
    glm::vec4 baseColorFactor;      // rgba (glTF baseColorFactor)
    glm::vec4 mr_ns_ac;             // x=metallicFactor, y=roughnessFactor, z=normalScale, w=alphaCutoff
    uint32_t  alphaMode;            // 0=OPAQUE, 1=MASK, 2=BLEND
    uint32_t  _pad[3];              // pad to 16-byte boundary (std140)
};
struct Material
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;


    VkImage mrImage = VK_NULL_HANDLE;
    VkDeviceMemory mrMemory = VK_NULL_HANDLE;
    VkImageView mrImageView = VK_NULL_HANDLE;
    VkSampler mrSampler = VK_NULL_HANDLE;

    VkImage normalImage = VK_NULL_HANDLE;
    VkDeviceMemory normalMemory = VK_NULL_HANDLE;
    VkImageView normalImageView = VK_NULL_HANDLE;
    VkSampler normalSampler = VK_NULL_HANDLE;

    VkImage emissiveImage = VK_NULL_HANDLE;
    VkDeviceMemory emissiveMemory = VK_NULL_HANDLE;
    VkImageView emissiveImageView = VK_NULL_HANDLE;
    VkSampler emissiveSampler = VK_NULL_HANDLE;
    //glm::vec3 emissiveFactor_andAO = glm::vec3(1.0f);

    VkImage aoImage = VK_NULL_HANDLE;
    VkDeviceMemory aoMemory = VK_NULL_HANDLE;
    VkImageView aoImageView = VK_NULL_HANDLE;
    VkSampler aoSampler = VK_NULL_HANDLE;
    bool aoAliasedMR = false;


    VkBuffer        paramsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory  paramsMemory = VK_NULL_HANDLE;
    MaterialParams  paramsCPU; // optional cached CPU copy
};