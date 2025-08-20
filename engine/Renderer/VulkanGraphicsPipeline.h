#pragma once
#include <vulkan/vulkan.h>
#include <string>

class VulkanGraphicsPipeline
{
public:
    VulkanGraphicsPipeline(VkDevice device, VkFormat colorFormat,VkFormat depthFormat);
    ~VulkanGraphicsPipeline();

    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getLayout() const { return pipelineLayout; }
    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

private:
    VkDevice device;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkFormat depthFormat;

    VkShaderModule loadShaderModule(const std::string &filepath);
    void createGraphicsPipeline(VkFormat colorFormat);
};
