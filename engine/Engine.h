#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "Renderer/VulkanGraphicsPipeline.h"
#include "Renderer/VulkanContext.h"
#include "Renderer/BufferUtils.h"
#include "Renderer/TextureUtils.h"
#include "Renderer/ModelLoader.h" // <- contains the Mesh struct
#include <vector>
#include "Renderer/SceneObject.h"
class Engine
{
public:
    Engine();
    ~Engine();

    void run();

private:
    GLFWwindow *window = nullptr;
    VulkanContext *vkContext = nullptr;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

   

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;


    // Camera orbit parameters
    float yaw = 0.0f;    // left/right
    float pitch = 0.0f;  // up/down
    float radius = 5.0f; // zoom

    glm::vec3 target = glm::vec3(0.0f); // model center
    glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

    std::vector<SceneObject> sceneObjects;

    VulkanGraphicsPipeline *pipeline = nullptr;

    void init();
    void mainLoop();
    void drawFrame(VkCommandBuffer commandBuffer, VulkanGraphicsPipeline &pipeline);
    void cleanup();
};
