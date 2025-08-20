#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D baseColorTex;
// layout(set = 0, binding = 1) uniform sampler2D metallicRoughnessTex;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    vec3 lightPos;
    vec3 viewPos;
} pc;

void main()
{
    vec3 albedo = texture(baseColorTex, fragUV).rgb;

    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(pc.lightPos - fragPos);
    vec3 viewDir = normalize(pc.viewPos - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    float diff = max(dot(normal, lightDir), 0.0);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);

    vec3 ambient = 0.05 * albedo;
    vec3 diffuse = diff * albedo;
    vec3 specular = spec * vec3(0.2); // low specular

    vec3 result = ambient + diffuse + specular;
    outColor = vec4(result, 1.0);
}
