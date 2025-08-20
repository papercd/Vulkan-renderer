#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct CustomPushConstants {
    glm::mat4 model;
    glm::mat4 viewProj;
    glm::vec3 lightPos;
    glm::vec3 viewPos;
};

struct VulkanBuffer
{
    VkBuffer buffer;
    VkDeviceMemory memory;
};

VulkanBuffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                          VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties);
void copyDataToBuffer(VkDevice device, VkDeviceMemory memory,
                      const void *data, VkDeviceSize size);
