#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_NONE
#include "Engine.h"
#include "Renderer/BufferUtils.h"
#include "Renderer/ModelLoader.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // ✅ This is required
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include "Renderer/VulkanContext.h"

Engine::Engine()
{
    // Constructor body (can be empty)
}

Engine::~Engine()
{
    // Destructor body (can be empty)
}

void Engine::run()
{
    init();
    std ::cout << "Vulkan Engine Initialized" << std::endl;
    std::cout << "Starting main loop..." << std::endl;
    mainLoop();
    std::cout << "Exiting main loop, cleaning up resources..." << std::endl;
    cleanup();
}

void Engine::init()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    this->window = glfwCreateWindow(800, 600, "Vulkan Game", nullptr, nullptr);

    std::cout << "creating Vulkan context..." << std::endl;
    vkContext = new VulkanContext(this->window);

    std::cout << "creating Vulkan graphics pipeline..." << std::endl;
    this->pipeline = new VulkanGraphicsPipeline(vkContext->getDevice(), vkContext->getSwapchainImageFormat());

    std::cout << "creating command pool and buffers..." << std::endl;
    // 1. Create Command Pool
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = 0; // ← or use vkbootstrap to get correct queue index
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    vkCreateCommandPool(vkContext->getDevice(), &poolInfo, nullptr, &commandPool);

    std::cout << "allocating command buffer..." << std::endl;
    // 2. Allocate Command Buffer
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(vkContext->getDevice(), &allocInfo, &commandBuffer);

    VkDevice device = vkContext->getDevice();

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailable);
    vkCreateSemaphore(device, &semInfo, nullptr, &renderFinished);

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fenceInfo, nullptr, &inFlight);

    sceneObjects.push_back(loadGLTFModelToSceneObject("../../assets/6_Pounder_Brass_Cannon.glb", vkContext->getDevice(), vkContext->getPhysicalDevice(), commandPool, vkContext->getGraphicsQueue(), pipeline->getDescriptorPool(), pipeline->getDescriptorSetLayout()));
}

void Engine::mainLoop()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        const float orbitSpeed = 0.01f;
        const float zoomSpeed = 0.2f;
        const float panSpeed = 0.1f;

        // Orbit controls
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
            yaw -= orbitSpeed;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
            yaw += orbitSpeed;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            pitch += orbitSpeed;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            pitch -= orbitSpeed;

        // Clamp pitch to avoid flipping
        pitch = glm::clamp(pitch, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);

        // Zoom in/out
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            radius = std::max(1.0f, radius - zoomSpeed);
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            radius = std::min(50.0f, radius + zoomSpeed);

        // --- WASD pan controls ---
        // Compute camera direction vectors
        glm::vec3 cameraDir = glm::normalize(target - cameraPos); // point toward target
        glm::vec3 right = glm::normalize(glm::cross(cameraDir, cameraUp));
        glm::vec3 up = glm::normalize(glm::cross(right, cameraDir));

        // Move the target (pan)
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            target += up * panSpeed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            target -= up * panSpeed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            target -= right * panSpeed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            target += right * panSpeed;

        // --- Recalculate camera position based on orbit ---
        cameraPos = glm::vec3(
                        radius * cos(pitch) * sin(yaw),
                        radius * sin(pitch),
                        radius * cos(pitch) * cos(yaw)) +
                    target;

        drawFrame(commandBuffer, *pipeline);
    }
}

void Engine::drawFrame(
    VkCommandBuffer commandBuffer,
    VulkanGraphicsPipeline &pipeline

)
{
    VkDevice device = vkContext->getDevice();

    // === [1] Sync with GPU ===

    std::cout << "[Frame] Waiting on GPU fence..." << std::endl;
    vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlight);
    std::cout << "[Frame] Fence reset, starting frame." << std::endl;

    // === [2] Acquire next image ===
    VkQueue graphicsQueue = vkContext->getGraphicsQueue();
    VkQueue presentQueue = vkContext->getPresentQueue();
    VkSwapchainKHR swapchain = vkContext->getSwapchain();
    auto &imageViews = vkContext->getSwapchainImageViews();
    VkExtent2D extent = vkContext->getSwapchainExtent();

    uint32_t imageIndex;
    std::cout << "[Frame] Acquiring next swapchain image..." << std::endl;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result != VK_SUCCESS)
    {
        std::cerr << "[Error] Failed to acquire swapchain image.\n";
        return;
    }

    // === [3] Begin command buffer ===
    std::cout << "[Frame] Resetting and beginning command buffer..." << std::endl;
    vkResetCommandBuffer(commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // === [4] Transition layout ===
    std::cout << "[Frame] Transitioning image layout to COLOR_ATTACHMENT_OPTIMAL..." << std::endl;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    barrier.image = vkContext->getSwapchainImages()[imageIndex];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // === [5] Begin rendering ===
    std::cout << "[Frame] Beginning dynamic rendering..." << std::endl;
    VkClearValue clearColor = {{0.1f, 0.1f, 0.1f, 1.0f}};
    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = imageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = clearColor;

    VkRenderingInfo renderInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderInfo.renderArea = {{0, 0}, extent};
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &renderInfo);

    // === [6] Set viewport & scissor ===
    std::cout << "[Frame] Setting viewport and scissor..." << std::endl;
    VkViewport viewport{0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // === [7] Bind pipeline and buffers ===
    std::cout << "[Frame] Binding pipeline and drawing mesh..." << std::endl;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.getPipeline());

    // camera view and projection
    glm::mat4 view = glm::lookAt(cameraPos, target, cameraUp);

    glm::mat4 proj = glm::perspective(glm::radians(45.0f), extent.width / (float)extent.height, 0.1f, 100.0f);

    proj[1][1] *= -1;

    for (const SceneObject &obj : sceneObjects)
    {
        for (const GPUMesh &gpu : obj.meshes)
        {
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &gpu.vertexBuffer.buffer, offsets);
            vkCmdBindIndexBuffer(commandBuffer, gpu.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            glm::mat4 mvp = proj * view * obj.transform;
            vkCmdPushConstants(commandBuffer, pipeline.getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);
            if (gpu.material && gpu.material->descriptorSet != VK_NULL_HANDLE)
            {
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline.getLayout(),
                    0, 1, &gpu.material->descriptorSet,
                    0, nullptr);
            }
            vkCmdDrawIndexed(commandBuffer, gpu.indexCount, 1, 0, 0, 0);
        }
    }

    // === [8] End rendering and recording ===
    std::cout << "[Frame] Ending rendering and command buffer..." << std::endl;
    vkCmdEndRendering(commandBuffer);
    // === [8.5] Transition image layout back to PRESENT_SRC_KHR ===
    std::cout << "[Frame] Transitioning image layout to PRESENT_SRC_KHR..." << std::endl;

    VkImageMemoryBarrier presentBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    presentBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    presentBarrier.image = vkContext->getSwapchainImages()[imageIndex];
    presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    presentBarrier.subresourceRange.baseMipLevel = 0;
    presentBarrier.subresourceRange.levelCount = 1;
    presentBarrier.subresourceRange.baseArrayLayer = 0;
    presentBarrier.subresourceRange.layerCount = 1;
    presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    presentBarrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &presentBarrier);

    vkEndCommandBuffer(commandBuffer);

    // === [9] Submit to graphics queue ===
    std::cout << "[Frame] Submitting command buffer to GPU..." << std::endl;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlight) != VK_SUCCESS)
    {
        std::cerr << "[Error] Failed to submit command buffer.\n";
        return;
    }

    // === [10] Present image ===
    std::cout << "[Frame] Presenting rendered image..." << std::endl;
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    if (vkQueuePresentKHR(presentQueue, &presentInfo) != VK_SUCCESS)
    {
        std::cerr << "[Error] Failed to present swapchain image.\n";
        return;
    }

    // === [11] Final GPU wait for frame completion ===
    std::cout << "[Frame] Frame complete. Waiting for fence to signal..." << std::endl;
    vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
    std::cout << "[Frame] Done.\n"
              << std::endl;
}

void Engine::cleanup()
{

    VkDevice device = vkContext->getDevice();

    vkDeviceWaitIdle(device);

    // Cleanup each SceneObject's buffers
    for (auto &obj : sceneObjects)
    {
        for (auto &gpu : obj.meshes)
        {
            vkDestroyBuffer(device, gpu.vertexBuffer.buffer, nullptr);
            vkFreeMemory(device, gpu.vertexBuffer.memory, nullptr);
            vkDestroyBuffer(device, gpu.indexBuffer.buffer, nullptr);
            vkFreeMemory(device, gpu.indexBuffer.memory, nullptr);
        }
    }

    vkDestroyCommandPool(device, commandPool, nullptr);
    delete pipeline;

    vkDestroyFence(device, inFlight, nullptr);
    vkDestroySemaphore(device, imageAvailable, nullptr);
    vkDestroySemaphore(device, renderFinished, nullptr);

    delete vkContext;

    glfwDestroyWindow(window);
    glfwTerminate();
}
