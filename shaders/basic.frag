#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

// Texture samplers
layout(set = 0, binding = 0) uniform sampler2D baseColorTex;
layout(set = 0, binding = 1) uniform sampler2D metallicRoughnessTex;
layout(set = 0, binding = 2) uniform sampler2D normalMapTex;

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    vec3 lightPos;
    vec3 viewPos;
} pc;

void main()
{
    // Sample textures
    vec3 albedo = texture(baseColorTex, fragUV).rgb;

    vec2 mr = texture(metallicRoughnessTex, fragUV).bg; // G = roughness, B = metallic
    float roughness = clamp(mr.x, 0.04, 1.0);
    float metallic = clamp(mr.y, 0.0, 1.0);

    // Compute lighting vectors
    
    vec3 n = texture(normalMapTex, fragUV).xyz * 2.0 - 1.0; // tangent-space normal
    vec3 normal = normalize(n); // for now, treat it in world space

    vec3 lightDir = normalize(pc.lightPos - fragPos);
    vec3 viewDir = normalize(pc.viewPos - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);

    // Specular (Blinn)
    float spec = pow(max(dot(normal, halfwayDir), 0.0), mix(1.0, 128.0, 1.0 - roughness));

    // Lighting components
    vec3 ambient = 0.05 * albedo;
    vec3 diffuse = diff * albedo * (1.0 - metallic);
    vec3 specular = spec * mix(vec3(1.0), albedo, metallic);

    vec3 result = ambient + diffuse + specular;
    outColor = vec4(result, 1.0);
}
