#include "BufferUtils.h"
#include <stdexcept>
#include <cstring>

struct PushConstants {
    glm::mat4 model; 
    glm::mat4 viewProj; 
    glm::vec3 lightPos; 
    glm::vec3 viewPos;
};

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("No suitable memory type found.");
}

VulkanBuffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                          VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties)
{
    VulkanBuffer vb{};
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &vb.buffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer!");

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, vb.buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &vb.memory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory!");

    vkBindBufferMemory(device, vb.buffer, vb.memory, 0);
    return vb;
}

void copyDataToBuffer(VkDevice device, VkDeviceMemory memory, const void *data, VkDeviceSize size)
{
    void *dst;
    vkMapMemory(device, memory, 0, size, 0, &dst);
    memcpy(dst, data, (size_t)size);
    vkUnmapMemory(device, memory);
}
