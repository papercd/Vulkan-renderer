#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

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

// temporary: 
layout(set = 0, binding =4,std140) uniform MaterialParams{
    vec4 emissiveFactor_andAO;
    vec4 baseColorFactor; 
    vec4 MRNSAC; 
    uint alphaMode; 
};

void main()
{
    vec4 baseTex = texture(baseColorTex,fragUV);
    vec4 base = baseTex * baseColorFactor; 

    vec2 mrTex = texture(metallicRoughnessTex,fragUV).bg;
    float roughness = clamp(mrTex.x * MRNSAC.y, 0.04, 1.0);
    float metallic = clamp(mrTex.y * MRNSAC.x , 0.0, 1.0);

    vec3 tspaceN = texture(normalMapTex,fragUV).xyz * 2.0 - 1.0;
    tspaceN.xy *= MRNSAC.z;

    // TODO: replace this with tbn when it is wired 
    vec3 normal = normalize(tspaceN * 0.0 + fragNormal);

    vec3 emissive = texture(emissiveTex, fragUV).rgb * emissiveFactor_andAO.rgb;

    // AO 
    float occ = texture(occlusionTex , fragUV).r;
    float ao = mix(1.0, occ, clamp(emissiveFactor_andAO.w, 0.0,1.0));

    // Lighting as before, but use base.rgb
    vec3 albedo = base.rgb;

    // (ambient/diffuse/spec calculations)
    vec3 ambient = 0.05 * albedo * ao; 

    // alpha handling
    float alpha = base.a; 

    // alpha mode
    if (alphaMode == 1u){
        if (alpha < MRNSAC.w) discard; 
    } else if (alphaMode == 2u){

    } else{
        alpha =1.0;
    }

    // diffuse 
    vec3 lightDir = normalize(pc.lightPos - fragPos);
    vec3 viewDir = normalize(pc.viewPos - fragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    float diff = max(dot(normal, lightDir), 0.0);

    vec3 diffuse = diff * albedo * (1.0 - metallic);

    // specular
    float spec = pow(max(dot(normal, halfwayDir), 0.0), mix(1.0, 128.0, 1.0 - roughness));

    vec3 specular = spec * mix(vec3(1.0), albedo, metallic);

    vec3 lit = ambient + diffuse + specular; 
    vec3 color = lit + emissive;

    outColor = vec4(color, 1.0);
}
