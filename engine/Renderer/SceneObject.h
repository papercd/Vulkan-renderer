#pragma once
#include "GPUMesh.h"
#include <glm/glm.hpp>
#include <vector>

struct SceneObject
{
    std::vector<GPUMesh> meshes;
    glm::mat4 transform;
};