#include "TextureUtils.h"
#include <cassert>
#include <cstring> // memcpy
#include <algorithm>
static uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{}; vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    assert(false && "No suitable memory type");
    return 0;
}

static VkCommandBuffer beginST(VkDevice dev, VkCommandPool pool) {
    VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    a.commandPool = pool; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(dev, &a, &cmd);
    VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &b); return cmd;
}
static void endST(VkDevice dev, VkQueue q, VkCommandPool p, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO}; s.commandBufferCount = 1; s.pCommandBuffers = &cmd;
    vkQueueSubmit(q, 1, &s, VK_NULL_HANDLE); vkQueueWaitIdle(q);
    vkFreeCommandBuffers(dev, p, 1, &cmd);
}

static void transition(VkCommandBuffer cmd, VkImage img, VkImageLayout oldL, VkImageLayout newL) {
    VkImageMemoryBarrier br{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    br.oldLayout = oldL; br.newLayout = newL; br.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; br.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    br.image = img;
    br.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    br.subresourceRange.levelCount = 1; br.subresourceRange.layerCount = 1;
    VkPipelineStageFlags src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        br.srcAccessMask = 0; br.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        br.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; br.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        assert(false && "Unsupported transition");
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &br);
}

static void copyBufToImg(VkCommandBuffer cmd, VkBuffer buf, VkImage img) {
    VkBufferImageCopy r{}; r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; r.imageSubresource.layerCount = 1;
    r.imageExtent = {1,1,1};
    vkCmdCopyBufferToImage(cmd, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
}

bool makeSolid1x1Texture(VkPhysicalDevice phys, VkDevice dev,
                         VkCommandPool pool, VkQueue queue,
                         VkFormat format,
                         const uint8_t rgba[4],
                         VkFilter filter,
                         Solid1x1& outTex)
{
    outTex = {}; outTex.format = format;

    // 1) Image
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {1,1,1};
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(dev, &ci, nullptr, &outTex.image) != VK_SUCCESS) return false;

    VkMemoryRequirements imReq{}; vkGetImageMemoryRequirements(dev, outTex.image, &imReq);
    VkMemoryAllocateInfo imAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    imAlloc.allocationSize = imReq.size;
    imAlloc.memoryTypeIndex = findMemoryType(phys, imReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(dev, &imAlloc, nullptr, &outTex.memory) != VK_SUCCESS) {
        vkDestroyImage(dev, outTex.image, nullptr); outTex.image = VK_NULL_HANDLE; return false;
    }
    vkBindImageMemory(dev, outTex.image, outTex.memory, 0);

    // 2) Staging buffer
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = 4; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging; if (vkCreateBuffer(dev, &bi, nullptr, &staging) != VK_SUCCESS) return false;
    VkMemoryRequirements br{}; vkGetBufferMemoryRequirements(dev, staging, &br);
    VkMemoryAllocateInfo ba{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ba.allocationSize = br.size;
    ba.memoryTypeIndex = findMemoryType(phys, br.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem; if (vkAllocateMemory(dev, &ba, nullptr, &stagingMem) != VK_SUCCESS) { vkDestroyBuffer(dev, staging, nullptr); return false; }
    vkBindBufferMemory(dev, staging, stagingMem, 0);
    void* map = nullptr; vkMapMemory(dev, stagingMem, 0, 4, 0, &map);
    std::memcpy(map, rgba, 4);
    vkUnmapMemory(dev, stagingMem);

    // 3) Copy + transitions
    VkCommandBuffer cmd = beginST(dev, pool);
    transition(cmd, outTex.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufToImg(cmd, staging, outTex.image);
    transition(cmd, outTex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    endST(dev, queue, pool, cmd);

    // 4) View
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = outTex.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(dev, &vi, nullptr, &outTex.view) != VK_SUCCESS) return false;

    // 5) Sampler
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = filter; si.minFilter = filter;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod = 0.0f; si.maxLod = 0.0f; si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    if (vkCreateSampler(dev, &si, nullptr, &outTex.sampler) != VK_SUCCESS) return false;

    // 6) Cleanup staging
    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);

    return true;
}

void destroySolid1x1Texture(VkDevice dev, const Solid1x1& t) {
    if (t.sampler) vkDestroySampler(dev, t.sampler, nullptr);
    if (t.view)    vkDestroyImageView(dev, t.view, nullptr);
    if (t.image)   vkDestroyImage(dev, t.image, nullptr);
    if (t.memory)  vkFreeMemory(dev, t.memory, nullptr);
}

// Wrappers

bool makeBlackSRGBTexture(VkPhysicalDevice p, VkDevice d, VkCommandPool pool, VkQueue q, Solid1x1& out){
    const uint8_t px[4] = {0,0,0,255};
    return makeSolid1x1Texture(p,d,pool,q, VK_FORMAT_R8G8B8A8_SRGB, px, VK_FILTER_LINEAR, out);
}

bool makeWhiteSRGBTexture(VkPhysicalDevice p, VkDevice d, VkCommandPool pool, VkQueue q, Solid1x1& out) {
    const uint8_t px[4] = {255,255,255,255}; // RGBA
    return makeSolid1x1Texture(p,d,pool,q, VK_FORMAT_R8G8B8A8_SRGB, px, VK_FILTER_LINEAR, out);
}

bool makeWhiteUNORMTexture(VkPhysicalDevice p, VkDevice d, VkCommandPool pool, VkQueue q, Solid1x1& out){
    const uint8_t px[4] = {255,255,255,255}; // RGBA
    return makeSolid1x1Texture(p,d,pool,q, VK_FORMAT_R8G8B8A8_UNORM, px, VK_FILTER_NEAREST, out);
}


bool makeFlatNormalUNORM(VkPhysicalDevice p, VkDevice d, VkCommandPool pool, VkQueue q, Solid1x1& out) {
    const uint8_t px[4] = {128,128,255,255}; // (0.5,0.5,1,1)
    return makeSolid1x1Texture(p,d,pool,q, VK_FORMAT_R8G8B8A8_UNORM, px, VK_FILTER_NEAREST, out);
}

bool makeDefaultMR_UNORM(VkPhysicalDevice p, VkDevice d, VkCommandPool pool, VkQueue q, Solid1x1& out) {
    // R=occlusion(1), G=roughness(1), B=metallic(0), A=1
    const uint8_t px[4] = {255,255,0,255};
    return makeSolid1x1Texture(p,d,pool,q, VK_FORMAT_R8G8B8A8_UNORM, px, VK_FILTER_NEAREST, out);
}