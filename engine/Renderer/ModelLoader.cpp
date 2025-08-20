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

    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& mat = model.materials[i];
        materials[i] = createMaterialFromGLTFTextures(
                mat,
                model,
                device,
                physicalDevice,
                commandPool,
                graphicsQueue,
                descriptorPool,
                descriptorSetLayout
        );
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

Material* createMaterialFromGLTFTextures(
    const tinygltf::Material& gltfMaterial,
    const tinygltf::Model& model,
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout descriptorSetLayout)
{
    Material* mat = new Material();

    auto loadImage = [&](const tinygltf::Image& gltfImage,
                     VkFormat format,
                     VkImage& image,
                     VkDeviceMemory& memory,
                     VkImageView& view,
                     VkSampler& sampler) {

        VkDeviceSize imageSize = gltfImage.image.size();

        VulkanBuffer staging = createBuffer(
            device, physicalDevice, imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        copyDataToBuffer(device, staging.memory, gltfImage.image.data(), imageSize);

        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = gltfImage.width;
        imageInfo.extent.height = gltfImage.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        vkCreateImage(device, &imageInfo, nullptr, &image);

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, image, &memReqs);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReqs.memoryTypeBits & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                allocInfo.memoryTypeIndex = i;
                break;
            }
        }

        vkAllocateMemory(device, &allocInfo, nullptr, &memory);
        vkBindImageMemory(device, image, memory, 0);

        // Transition layout and copy
        VkCommandBufferAllocateInfo allocInfoCmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfoCmd.commandPool = commandPool;
        allocInfoCmd.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &allocInfoCmd, &cmd);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(gltfImage.width), static_cast<uint32_t>(gltfImage.height), 1};

        vkCmdCopyBufferToImage(cmd, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);

        // Create image view
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(device, &viewInfo, nullptr, &view); // ✅


        // Create sampler
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
    };

    // Load base color texture
    if (gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0) {
        const auto& img = model.images[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index];
        loadImage(img, VK_FORMAT_R8G8B8A8_SRGB, mat->image, mat->imageMemory, mat->imageView, mat->sampler);

    }

    // Load metallic-roughness texture
    if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
        const auto& img = model.images[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index];
        loadImage(img, VK_FORMAT_R8G8B8A8_UNORM, mat->mrImage, mat->mrMemory, mat->mrImageView, mat->mrSampler);

    }

    if (gltfMaterial.normalTexture.index >= 0){
        const auto& img = model.images[gltfMaterial.normalTexture.index];
        loadImage(img,VK_FORMAT_R8G8B8A8_UNORM ,mat->normalImage, mat->normalMemory, mat->normalImageView, mat->normalSampler);
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfoDesc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfoDesc.descriptorPool = descriptorPool;
    allocInfoDesc.descriptorSetCount = 1;
    allocInfoDesc.pSetLayouts = &descriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfoDesc, &mat->descriptorSet);

    // Descriptor writes
    std::array<VkWriteDescriptorSet, 3> writes{};

    VkDescriptorImageInfo baseInfo{};
    baseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    baseInfo.imageView = mat->imageView;
    baseInfo.sampler = mat->sampler;

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = mat->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &baseInfo;

    VkDescriptorImageInfo mrInfo{};
    mrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    mrInfo.imageView = mat->mrImageView;
    mrInfo.sampler = mat->mrSampler;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = mat->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &mrInfo;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = mat->normalImageView;
    normalInfo.sampler = mat->normalSampler;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = mat->descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &normalInfo;

    uint32_t writeCount = 1;

    if (gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
        // Fill writes[1] like you're doing now
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mat->descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &mrInfo;

        writeCount = 2; // ✅ Tell Vulkan you want to write both
    }

    vkUpdateDescriptorSets(device, writeCount, writes.data(), 0, nullptr);



    //kUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    return mat;
}
