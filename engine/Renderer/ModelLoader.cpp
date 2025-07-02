#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stdexcept>
#include <cstring>
#include "ModelLoader.h"
#include <glm/gtc/type_ptr.hpp> // for glm::make_vec3 / make_vec2
#include <iostream>

SceneObject loadGLTFModelToSceneObject(const std::string &path, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout)
{
    SceneObject obj;
    obj.transform = glm::mat4(1.0f);

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool success = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!success)
        throw std::runtime_error("Failed to load model: " + path + " - " + err);
    if (!warn.empty())
        std::cout << "glTF warning: " << warn << "\n";

    const auto &buffer = model.buffers[0]; // assume single buffer for now

    std::vector<Material *> materials(model.images.size());

    for (size_t i = 0; i < model.images.size(); ++i)
    {
        materials[i] = createMaterialFromGLTFImage(
            model.images[i],
            device,
            physicalDevice,
            commandPool,
            graphicsQueue,
            descriptorPool,
            descriptorSetLayout);
    }

    for (const auto &mesh : model.meshes)
    {
        for (const auto &primitive : mesh.primitives)
        {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            const auto &posAccessor = model.accessors[primitive.attributes.at("POSITION")];
            const auto &normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
            const auto &uvAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];

            const float *posData = reinterpret_cast<const float *>(
                &buffer.data[model.bufferViews[posAccessor.bufferView].byteOffset + posAccessor.byteOffset]);
            const float *normData = reinterpret_cast<const float *>(
                &buffer.data[model.bufferViews[normAccessor.bufferView].byteOffset + normAccessor.byteOffset]);
            const float *uvData = reinterpret_cast<const float *>(
                &buffer.data[model.bufferViews[uvAccessor.bufferView].byteOffset + uvAccessor.byteOffset]);

            std::vector<Vertex> vertices(posAccessor.count);
            for (size_t i = 0; i < posAccessor.count; ++i)
            {
                vertices[i].pos = glm::make_vec3(&posData[i * 3]);
                vertices[i].normal = glm::make_vec3(&normData[i * 3]);
                vertices[i].texCoord = glm::make_vec2(&uvData[i * 2]);
            }

            const auto &idxAccessor = model.accessors[primitive.indices];
            const void *indexData = &buffer.data[model.bufferViews[idxAccessor.bufferView].byteOffset + idxAccessor.byteOffset];

            std::vector<uint32_t> indices(idxAccessor.count);
            if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                const uint16_t *src = static_cast<const uint16_t *>(indexData);
                for (size_t i = 0; i < indices.size(); ++i)
                    indices[i] = src[i];
            }
            else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                const uint32_t *src = static_cast<const uint32_t *>(indexData);
                for (size_t i = 0; i < indices.size(); ++i)
                    indices[i] = src[i];
            }
            else
            {
                std::cerr << "Unsupported index type.\n";
                continue;
            }

            // GPU buffer creation
            GPUMesh gpu;
            gpu.vertexBuffer = createBuffer(device, physicalDevice,
                                            sizeof(Vertex) * vertices.size(),
                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            copyDataToBuffer(device, gpu.vertexBuffer.memory, vertices.data(), sizeof(Vertex) * vertices.size());

            gpu.indexBuffer = createBuffer(device, physicalDevice,
                                           sizeof(uint32_t) * indices.size(),
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            copyDataToBuffer(device, gpu.indexBuffer.memory, indices.data(), sizeof(uint32_t) * indices.size());

            gpu.indexCount = static_cast<uint32_t>(indices.size());

            if (primitive.material >= 0 && primitive.material < model.materials.size())
            {
                const auto &mat = model.materials[primitive.material];
                int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
                if (texIndex >= 0 && texIndex < materials.size())
                {
                    gpu.material = materials[texIndex];
                }
            }

            obj.meshes.push_back(gpu);
        }
    }

    return obj;
}

void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = image;
    barrier.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

Material *createMaterialFromGLTFImage(
    const tinygltf::Image &gltfImage,
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout descriptorSetLayout)
{
    Material *mat = new Material();

    VkDeviceSize imageSize = gltfImage.image.size();

    // 1. Create staging buffer
    VulkanBuffer staging = createBuffer(
        device, physicalDevice, imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    copyDataToBuffer(device, staging.memory, gltfImage.image.data(), imageSize);

    // 2. Create image
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = gltfImage.width;
    imageInfo.extent.height = gltfImage.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    vkCreateImage(device, &imageInfo, nullptr, &mat->image);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, mat->image, &memReqs);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReqs.size;

    // Find memory type
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((memReqs.memoryTypeBits & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }

    vkAllocateMemory(device, &allocInfo, nullptr, &mat->imageMemory);
    vkBindImageMemory(device, mat->image, mat->imageMemory, 0);

    // 3. Transition + Copy
    VkCommandBufferAllocateInfo allocInfoCmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfoCmd.commandPool = commandPool;
    allocInfoCmd.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfoCmd, &cmd);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    transitionImageLayout(cmd, mat->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(gltfImage.width), static_cast<uint32_t>(gltfImage.height), 1};

    vkCmdCopyBufferToImage(cmd, staging.buffer, mat->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transitionImageLayout(cmd, mat->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    // 4. Image view
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = mat->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(device, &viewInfo, nullptr, &mat->imageView);

    // 5. Sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    vkCreateSampler(device, &samplerInfo, nullptr, &mat->sampler);

    VkDescriptorSetAllocateInfo allocInfoDesc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfoDesc.descriptorPool = descriptorPool;
    allocInfoDesc.descriptorSetCount = 1;
    allocInfoDesc.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfoDesc, &mat->descriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate descriptor set.");

    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = mat->imageView;
    descImageInfo.sampler = mat->sampler;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = mat->descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &descImageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    return mat;
}
