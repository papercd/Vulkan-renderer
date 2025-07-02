#pragma once
#include "Material.h"
#include "Renderer/BufferUtils.h"

struct GPUMesh
{
    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;

    uint32_t indexCount;
    Material *material = nullptr;
};
