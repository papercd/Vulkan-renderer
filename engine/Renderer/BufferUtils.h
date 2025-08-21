#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct CustomPushConstants {
    glm::mat4 model;        //  0–63
    glm::mat4 viewProj;     // 64–127
    alignas(16) glm::vec3 lightPos; // 128–143
    alignas(16) glm::vec3 viewPos;  // 144–159
};

struct VulkanBuffer
{
    VkBuffer buffer;
    VkDeviceMemory memory;
};

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags props);

VulkanBuffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                          VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties);
void copyDataToBuffer(VkDevice device, VkDeviceMemory memory,
                      const void *data, VkDeviceSize size);
