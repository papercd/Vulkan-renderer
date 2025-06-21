#pragma once

#include <vulkan/vulkan.h>

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
