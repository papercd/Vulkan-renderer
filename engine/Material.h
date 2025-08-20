#pragma once
#include <vulkan/vulkan.h>

struct Material
{
    VkImage image;
    VkDeviceMemory imageMemory;
    VkImageView imageView;
    VkSampler sampler;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;


    VkImage mrImage;
    VkDeviceMemory mrMemory;
    VkImageView mrImageView;
    VkSampler mrSampler;

};