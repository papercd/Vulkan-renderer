#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>

class VulkanContext
{
public:
    VulkanContext(GLFWwindow *window);
    ~VulkanContext();

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    VkDevice getDevice() const { return device; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    std::vector<VkImage> getSwapchainImages() const { return swapchainImages; }

    VkSwapchainKHR getSwapchain() const { return swapchain; }
    const std::vector<VkImageView> &getSwapchainImageViews() const { return swapchainImageViews; }
    VkExtent2D getSwapchainExtent() const { return swapchainExtent; }
    VkFormat getSwapchainImageFormat() const { return swapchainImageFormat; }

private:
    void init(GLFWwindow *window);
    void cleanup();

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
};
