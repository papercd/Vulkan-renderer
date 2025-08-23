#pragma once
#include <vulkan/vulkan.h>
#include <string>

class VulkanGraphicsPipeline
{
public:
    VulkanGraphicsPipeline(VkDevice device, VkFormat colorFormat,VkFormat depthFormat);
    ~VulkanGraphicsPipeline();

    VkPipeline getOpaquePipeline() const {return pipelineOpaque;}
    VkPipeline getBlendPipeline() const {return pipelineBlend;}

    //VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getLayout() const { return pipelineLayout; }
    VkDescriptorPool getDescriptorPool() const { return descriptorPool; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

    VkPipeline getPipelineOpaqueCullBack() const { return pipelineOpaqueCullBack; }
    VkPipeline getPipelineOpaqueCullNone() const { return pipelineOpaqueCullNone; }
    VkPipeline getPipelineBlendCullBack()  const { return pipelineBlendCullBack; }
    VkPipeline getPipelineBlendCullNone()  const { return pipelineBlendCullNone; }

private:
    VkDevice device;
    VkPipeline pipelineOpaque = VK_NULL_HANDLE;
    VkPipeline pipelineBlend  = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkFormat depthFormat;

    VkPipeline pipelineOpaqueCullBack = VK_NULL_HANDLE;
    VkPipeline pipelineOpaqueCullNone = VK_NULL_HANDLE;
    VkPipeline pipelineBlendCullBack  = VK_NULL_HANDLE;
    VkPipeline pipelineBlendCullNone  = VK_NULL_HANDLE;
    

    VkShaderModule loadShaderModule(const std::string &filepath);

    void createGraphicsPipelines(VkFormat colorFormat);
    VkPipeline buildPipelineVariant(VkFormat colorFormat, bool enableBlend, VkCullModeFlags cullMode);
};
