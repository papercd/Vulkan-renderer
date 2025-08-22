#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
// NEW inputs
layout(location = 3) in vec3 fragTangentWS;
layout(location = 4) in float fragTangentSign;

layout(location = 0) out vec4 outColor;

// Texture samplers
layout(set = 0, binding = 0) uniform sampler2D baseColorTex;
layout(set = 0, binding = 1) uniform sampler2D metallicRoughnessTex;
layout(set = 0, binding = 2) uniform sampler2D normalMapTex;
layout(set = 0, binding = 3) uniform sampler2D emissiveTex; 
layout(set = 0, binding = 5) uniform sampler2D occlusionTex;

// Push constants
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 viewProj;
    vec3 lightPos;
    vec3 viewPos;
} pc;

// Material UBO (unchanged)
layout(set = 0, binding = 4, std140) uniform MaterialParams {
    vec4 emissiveFactor_andAO; // rgb emissive, a = AO strength
    vec4 baseColorFactor; 
    vec4 MRNSAC;              // x=metallic, y=roughness, z=normalScale, w=alphaCutoff
    uint alphaMode;           // 0 OPAQUE, 1 MASK, 2 BLEND
};

void main()
{
    // Base color
    vec4 baseTex = texture(baseColorTex, fragUV);
    vec4 base    = baseTex * baseColorFactor; 

    // Metallic/Roughness (glTF: G=roughness, B=metallic)
    vec2 mrTex   = texture(metallicRoughnessTex, fragUV).bg;
    float roughness = clamp(mrTex.x * MRNSAC.y, 0.04, 1.0);
    float metallic  = clamp(mrTex.y * MRNSAC.x, 0.0, 1.0);

    // === TBN normal mapping ===
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangentWS);
    vec3 B = normalize(cross(N, T)) * fragTangentSign;  // handedness from glTF .w
    mat3 TBN = mat3(T, B, N);

    vec3 tspaceN = texture(normalMapTex, fragUV).xyz * 2.0 - 1.0;
    tspaceN.xy *= MRNSAC.z; // normalTexture.scale (XY only)
    vec3 normal = normalize(TBN * tspaceN);

    // Emissive & AO
    vec3 emissive = texture(emissiveTex, fragUV).rgb * emissiveFactor_andAO.rgb;
    float occ = texture(occlusionTex, fragUV).r;
    float ao  = mix(1.0, occ, clamp(emissiveFactor_andAO.a, 0.0, 1.0));

    // Alpha handling
    float alpha = base.a; 
    if (alphaMode == 1u) {
        if (alpha < MRNSAC.w) discard; 
    } else if (alphaMode == 2u) {
        // BLEND: keep alpha as is
    } else {
        alpha = 1.0;
    }

    // Simple lighting (your existing temp model)
    vec3 albedo = base.rgb;
    vec3 lightDir   = normalize(pc.lightPos - fragPos);
    vec3 viewDir    = normalize(pc.viewPos - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse  = diff * albedo * (1.0 - metallic);

    float specPow = mix(1.0, 128.0, 1.0 - roughness);
    float spec    = pow(max(dot(normal, halfwayDir), 0.0), specPow);
    vec3 specular = spec * mix(vec3(1.0), albedo, metallic);

    vec3 ambient = 0.05 * albedo * ao; 
    vec3 lit = ambient + diffuse + specular; 
    vec3 color = lit + emissive;

    outColor = vec4(color, alpha);
}
