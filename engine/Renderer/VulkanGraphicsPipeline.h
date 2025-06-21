#pragma once
#include <vulkan/vulkan.h>
#include <string>

class VulkanGraphicsPipeline
{
public:
    VulkanGraphicsPipeline(VkDevice device, VkFormat colorFormat);
    ~VulkanGraphicsPipeline();

    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getLayout() const { return pipelineLayout; }

private:
    VkDevice device;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkShaderModule loadShaderModule(const std::string &filepath);
    void createGraphicsPipeline(VkFormat colorFormat);
};
