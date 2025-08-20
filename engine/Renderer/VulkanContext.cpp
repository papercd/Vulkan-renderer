#include "VulkanContext.h"
#include <VkBootstrap.h>
#include <iostream>

void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}


VulkanContext::VulkanContext(GLFWwindow *window)
{
    init(window);
}

VulkanContext::~VulkanContext()
{
    cleanup();
}

void VulkanContext::init(GLFWwindow *window)
{
    vkb::InstanceBuilder builder;

    auto inst_ret = builder.set_app_name("VulkanGameEngine")
                        .request_validation_layers()
                        .use_default_debug_messenger()
                        .require_api_version(1, 3, 0)
                        .build();

    if (!inst_ret)
    {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }

    vkb::Instance vkb_inst = inst_ret.value();
    debugMessenger = vkb_inst.debug_messenger;
    instance = vkb_inst.instance;

    // Create surface from GLFW window
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface.");
    }

    // Choose a GPU
    vkb::PhysicalDeviceSelector selector{vkb_inst};
    vkb::PhysicalDevice physicalDeviceRet = selector
                                                .set_surface(surface)
                                                .select()
                                                .value();

    // Enable synchronization2 + dynamic rendering together
    VkPhysicalDeviceSynchronization2Features sync2Feature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    sync2Feature.synchronization2 = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeatures renderingFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    renderingFeature.dynamicRendering = VK_TRUE;
    renderingFeature.pNext = &sync2Feature; // <- chain sync2 behind rendering

    vkb::DeviceBuilder deviceBuilder{physicalDeviceRet};
    auto device_ret = deviceBuilder
                          .add_pNext(&renderingFeature)
                          .build();

    if (!device_ret)
    {
        throw std::runtime_error("Failed to create logical device with dynamic rendering + sync2.");
    }

    vkb::Device vkbDevice = device_ret.value();

    device = vkbDevice.device;
    physicalDevice = physicalDeviceRet.physical_device;

    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    presentQueue = vkbDevice.get_queue(vkb::QueueType::present).value();

    // --- SWAPCHAIN CREATION via vkbootstrap ---
    vkb::SwapchainBuilder swapchainBuilder{physicalDeviceRet, device, surface};


    auto swap_ret = swapchainBuilder
                        .use_default_format_selection()
                        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // vsync
                        .set_desired_extent(800, 600)                       // match window size
                        .build();

    if (!swap_ret)
    {
        throw std::runtime_error("Failed to create swapchain.");
    }

    vkb::Swapchain vkbSwapchain = swap_ret.value();
    swapchain = vkbSwapchain.swapchain;

    // Store image views and format/extents
    swapchainImages = vkbSwapchain.get_images().value();
    swapchainImageViews = vkbSwapchain.get_image_views().value();
    swapchainImageFormat = vkbSwapchain.image_format;
    swapchainExtent = vkbSwapchain.extent;

    std::vector<VkFormat> preferredDepthFormats = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };
    
    std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates){
        VkFormatProperties props; 
        vkGetPhysicalDeviceFormatProperties(physicalDevice,format,&props);

        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT){
            depthFormat = format;
            break;
        }
    }


    if (depthFormat == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("Failed to find supported depth format");
    }

    depthImages.resize(swapchainImages.size());
    depthImageMemories.resize(swapchainImages.size());
    depthImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); ++i) {
        // Create VkImage
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = swapchainExtent.width;
        imageInfo.extent.height = swapchainExtent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateImage(device, &imageInfo, nullptr, &depthImages[i]);

        // Allocate memory
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, depthImages[i], &memReqs);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t j = 0; j < memProps.memoryTypeCount; ++j) {
            if ((memReqs.memoryTypeBits & (1 << j)) &&
                (memProps.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                allocInfo.memoryTypeIndex = j;
                break;
            }
        }

        vkAllocateMemory(device, &allocInfo, nullptr, &depthImageMemories[i]);
        vkBindImageMemory(device, depthImages[i], depthImageMemories[i], 0);

        // Create VkImageView
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = depthImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(device, &viewInfo, nullptr, &depthImageViews[i]);
    }

    // --- Transition depth images to DEPTH_ATTACHMENT_OPTIMAL ---
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value(); // <-- correct graphics queue
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool commandPool;
    vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    for (size_t i = 0; i < depthImages.size(); ++i)
    {
        transitionImageLayout(
            cmd,
            depthImages[i],
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vkDestroyCommandPool(device, commandPool, nullptr);



    std::cout << "Vulkan context initialized.\n";
}

void VulkanContext::cleanup()
{

    for (auto view : depthImageViews)
        vkDestroyImageView(device, view, nullptr);
    for (auto img : depthImages)
        vkDestroyImage(device, img, nullptr);
    for (auto mem : depthImageMemories)
        vkFreeMemory(device, mem, nullptr);

    for (auto view : swapchainImageViews)
    {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);

    vkDestroyDevice(device, nullptr);
    if (debugMessenger)
    {
        auto vkDestroyDebugUtilsMessengerEXT =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}
