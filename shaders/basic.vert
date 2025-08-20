#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    vec3 lightPos;
    vec3 viewPos;
} pc;

void main() {
    fragPos = vec3(pc.model * vec4(inPos, 1.0));
    fragNormal = mat3(transpose(inverse(pc.model))) * inNormal; // world-space normal
    fragUV = inUV;

    gl_Position = pc.viewProj * vec4(fragPos, 1.0);
}
