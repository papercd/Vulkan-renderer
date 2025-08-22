#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
// NEW: glTF tangent (xyz = tangent, w = handedness)
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;   // world-space normal
layout(location = 2) out vec2 fragUV;
// NEW varyings
layout(location = 3) out vec3 fragTangentWS;
layout(location = 4) out float fragTangentSign;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    vec3 lightPos;
    vec3 viewPos;
} pc;

void main() {
    fragPos = vec3(pc.model * vec4(inPos, 1.0));
    mat3 Nmat = mat3(transpose(inverse(pc.model)));

    // Transform to world space
    fragNormal = normalize(Nmat * inNormal);

    // World-space tangent, orthonormalized against N
    vec3 T = normalize(Nmat * inTangent.xyz);
    T = normalize(T - fragNormal * dot(fragNormal, T));

    fragTangentWS   = T;
    fragTangentSign = inTangent.w;

    fragUV = inUV;
    gl_Position = pc.viewProj * vec4(fragPos, 1.0);
}
