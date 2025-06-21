#include "VulkanContext.h"
#include <VkBootstrap.h>
#include <iostream>

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

    std::cout << "Vulkan context initialized.\n";
}

void VulkanContext::cleanup()
{
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
