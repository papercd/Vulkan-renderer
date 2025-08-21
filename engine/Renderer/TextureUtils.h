#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

struct Solid1x1 {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view         = VK_NULL_HANDLE;
    VkSampler      sampler      = VK_NULL_HANDLE;
    VkFormat       format       = VK_FORMAT_UNDEFINED; 
};

struct FallbackTextures{
    Solid1x1 blackSRGB;
    Solid1x1 whiteSRGB;
    Solid1x1 flatNormal;
    Solid1x1 defaultMR;
    Solid1x1 whiteUNORM;
};



bool makeSolid1x1Texture(VkPhysicalDevice phys, VkDevice dev,
                         VkCommandPool cmdPool, VkQueue queue,
                         VkFormat format,
                         const uint8_t rgba[4],
                         VkFilter filter,
                         Solid1x1& outTex);

void destroySolid1x1Texture(VkDevice dev, const Solid1x1& tex);

// Convenience wrappers
bool makeBlackSRGBTexture(VkPhysicalDevice, VkDevice, VkCommandPool, VkQueue, Solid1x1& outTex);
bool makeWhiteSRGBTexture (VkPhysicalDevice, VkDevice, VkCommandPool, VkQueue, Solid1x1& outTex);
bool makeWhiteUNORMTexture(VkPhysicalDevice , VkDevice , VkCommandPool, VkQueue , Solid1x1& out);
bool makeFlatNormalUNORM  (VkPhysicalDevice, VkDevice, VkCommandPool, VkQueue, Solid1x1& outTex);
bool makeDefaultMR_UNORM  (VkPhysicalDevice, VkDevice, VkCommandPool, VkQueue, Solid1x1& outTex);
