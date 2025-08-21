#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stdexcept>
#include <cstring>
#include "ModelLoader.h"
#include <glm/gtc/type_ptr.hpp> // for glm::make_vec3 / make_vec2
#include <iostream>

SceneObject loadGLTFModelToSceneObject(const std::string &path, const FallbackTextures fbts, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout)
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
                fbts,
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
    const FallbackTextures fbts,
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


    // defaults from gltf spec 
    mat->paramsCPU.baseColorFactor = glm::vec4(1.0f);
    mat->paramsCPU.mr_ns_ac        = glm::vec4(1.0f,1.0f,1.0f,0.5f);
    mat->paramsCPU.alphaMode = 0; 

    // Load base color texture
    bool hasBaseColorTexture = gltfMaterial.pbrMetallicRoughness.baseColorTexture.index >= 0;
    if (hasBaseColorTexture) {
        const auto& img = model.images[gltfMaterial.pbrMetallicRoughness.baseColorTexture.index];
        loadImage(img, VK_FORMAT_R8G8B8A8_SRGB, mat->image, mat->imageMemory, mat->imageView, mat->sampler);

    }

    bool hasBaseColorFactor = gltfMaterial.pbrMetallicRoughness.baseColorFactor.size() == 4;
    if (hasBaseColorFactor){
        mat->paramsCPU.baseColorFactor = glm::vec4(
            (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[0],
            (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[1],
            (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[2],
            (float)gltfMaterial.pbrMetallicRoughness.baseColorFactor[3]
        );
    }

    bool hasMetallicFactor = gltfMaterial.pbrMetallicRoughness.metallicFactor >= 0.0;
    if (hasMetallicFactor){
        mat->paramsCPU.mr_ns_ac.x = (float)gltfMaterial.pbrMetallicRoughness.metallicFactor;
    }

    bool hasMetallicRoughness = gltfMaterial.pbrMetallicRoughness.roughnessFactor >= 0.0;
    if (hasMetallicRoughness){
        mat->paramsCPU.mr_ns_ac.y = (float)gltfMaterial.pbrMetallicRoughness.roughnessFactor;
    }
   

    // Load metallic-roughness texture
    bool hasMetallicRoughnessTexture = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0;
    if (hasMetallicRoughnessTexture) {
        const auto& img = model.images[gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index];
        loadImage(img, VK_FORMAT_R8G8B8A8_UNORM, mat->mrImage, mat->mrMemory, mat->mrImageView, mat->mrSampler);

    }

    // load normal texture 
    bool hasNormalTexture = gltfMaterial.normalTexture.index >= 0;
    if (hasNormalTexture){
        const auto& img = model.images[gltfMaterial.normalTexture.index];
        loadImage(img,VK_FORMAT_R8G8B8A8_UNORM ,mat->normalImage, mat->normalMemory, mat->normalImageView, mat->normalSampler);

        if (gltfMaterial.normalTexture.scale > 0.0){
            mat->paramsCPU.mr_ns_ac.z = (float)gltfMaterial.normalTexture.scale;
        }
    }

    // alphaMode 
    if (!gltfMaterial.alphaMode.empty()){
        if (gltfMaterial.alphaMode == "MASK") mat->paramsCPU.alphaMode =1;
        else if (gltfMaterial.alphaMode == "BLEND") mat->paramsCPU.alphaMode =2;
        else mat->paramsCPU.alphaMode = 0; 
    }

    if (gltfMaterial.alphaCutoff >0.0){
        mat->paramsCPU.mr_ns_ac.w = (float)gltfMaterial.alphaCutoff;
    }

    // read emissive factor and load emissive texture

    if (gltfMaterial.emissiveFactor.size() == 3){
        mat->paramsCPU.emissiveFactor_andAO = glm::vec4(
            (float)gltfMaterial.emissiveFactor[0],
            (float)gltfMaterial.emissiveFactor[1],
            (float)gltfMaterial.emissiveFactor[2],
            1.0f
        );
    } else{
        mat->paramsCPU.emissiveFactor_andAO = glm::vec4(1.0f);
    }

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = sizeof(MaterialParams);
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; 
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device,&bci,nullptr,&mat->paramsBuffer);
    
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, mat->paramsBuffer, &req);

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(physicalDevice,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(device, &mai, nullptr, &mat->paramsMemory);
    vkBindBufferMemory(device, mat->paramsBuffer, mat->paramsMemory, 0);

    // 3) Upload data
    void* mapped = nullptr;
    vkMapMemory(device, mat->paramsMemory, 0, sizeof(MaterialParams), 0, &mapped);
    std::memcpy(mapped, &mat->paramsCPU, sizeof(MaterialParams));
    vkUnmapMemory(device, mat->paramsMemory);
    
    bool hasEmissiveTexture = gltfMaterial.emissiveTexture.index >= 0;
    if (hasEmissiveTexture){
        const auto& img = model.images[gltfMaterial.emissiveTexture.index];
        loadImage(img, VK_FORMAT_R8G8B8A8_SRGB,  // SRGB for color
                mat->emissiveImage, mat->emissiveMemory,
                mat->emissiveImageView, mat->emissiveSampler);
    }

    bool hasAO = (gltfMaterial.occlusionTexture.index >= 0);

    // strength default = 1.0 (stored in UBO's .w)
    float occStrength = 1.0f;
    if (hasAO && gltfMaterial.occlusionTexture.strength > 0.0) {
        occStrength = static_cast<float>(gltfMaterial.occlusionTexture.strength);
    }
    mat->paramsCPU.emissiveFactor_andAO.w = occStrength; // xyz already set to emissiveFactor_andAO earlier

    if (hasAO) {
        const int aoTexIdx = gltfMaterial.occlusionTexture.index;
        const int aoImgIdx = model.textures[aoTexIdx].source;

        // If AO texture == MR texture, alias MR objects:
        const int mrTexIdx = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index;
        const bool shareWithMR = (mrTexIdx >= 0 && aoTexIdx == mrTexIdx);

        if (shareWithMR) {
            // IMPORTANT: assign *all* AO fields so later code sees non-null handles.
            mat->aoImage     = mat->mrImage;
            mat->aoImageView = mat->mrImageView;
            mat->aoSampler   = mat->mrSampler;
            mat->aoMemory    = mat->mrMemory;
            mat->aoAliasedMR = true; // add a bool to avoid double destroy
        } else {
            const auto& aoImg = model.images[aoImgIdx];
            loadImage(aoImg, VK_FORMAT_R8G8B8A8_UNORM,
                    mat->aoImage, mat->aoMemory,
                    mat->aoImageView, mat->aoSampler);
            mat->aoAliasedMR = false;
        }
    }
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfoDesc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfoDesc.descriptorPool = descriptorPool;
    allocInfoDesc.descriptorSetCount = 1;
    allocInfoDesc.pSetLayouts = &descriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocInfoDesc, &mat->descriptorSet);

    // Descriptor writes
    std::array<VkWriteDescriptorSet, 6> writes{};

    

    VkDescriptorImageInfo baseInfo{};
    baseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (hasBaseColorTexture){
        baseInfo.imageView = mat->imageView;
        baseInfo.sampler = mat->sampler;
    } else{
        baseInfo.imageView = fbts.whiteSRGB.view;
        baseInfo.sampler = fbts.whiteSRGB.sampler;
    }
    

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = mat->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &baseInfo;

    VkDescriptorImageInfo mrInfo{};
    mrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (hasMetallicRoughnessTexture){
        mrInfo.imageView = mat->mrImageView;
        mrInfo.sampler = mat->mrSampler;
    }else{
        mrInfo.imageView = fbts.defaultMR.view; 
        mrInfo.sampler = fbts.defaultMR.sampler; 
    }
    
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = mat->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &mrInfo;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (hasNormalTexture){
        normalInfo.imageView = mat->normalImageView;
        normalInfo.sampler = mat->normalSampler;
    } else{
        normalInfo.imageView = fbts.flatNormal.view;
        normalInfo.sampler = fbts.flatNormal.sampler; 
    }

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = mat->descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &normalInfo;

    VkDescriptorImageInfo emissiveInfo{};
    emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (hasEmissiveTexture){
        emissiveInfo.imageView = mat->emissiveImageView;
        emissiveInfo.sampler   = mat->emissiveSampler;
    } else{
        emissiveInfo.imageView = fbts.blackSRGB.view;
        emissiveInfo.sampler = fbts.blackSRGB.sampler;
    }
        
    
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = mat->descriptorSet;
    writes[3].dstBinding = 3; // emissive binding
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &emissiveInfo;   

    VkDescriptorBufferInfo matBufInfo{};
    matBufInfo.buffer = mat->paramsBuffer;
    matBufInfo.offset = 0;
    matBufInfo.range  = sizeof(MaterialParams);

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = mat->descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &matBufInfo;

    VkDescriptorImageInfo aoInfo{};
    aoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 

    if (mat->aoImageView != VK_NULL_HANDLE && mat->aoSampler != VK_NULL_HANDLE) {
        aoInfo.imageView = mat->aoImageView;
        aoInfo.sampler   = mat->aoSampler;
    } else {
        aoInfo.imageView = fbts.whiteUNORM.view;     // must be created at context init
        aoInfo.sampler   = fbts.whiteUNORM.sampler;
    }

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = mat->descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].descriptorCount = 1;
    writes[5].pImageInfo = &aoInfo;

    uint32_t writeCount = 6;

    vkUpdateDescriptorSets(device, writeCount, writes.data(), 0, nullptr);



    //kUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    return mat;
}
