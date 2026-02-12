#version 410 core

#define MAX_LIGHTS 8
#define MAX_SHADOW_MAPS_2D 16
#define MAX_SHADOW_MAPS_CUBE 2
#define PI 3.14159265359

struct Light {
    vec4 meta;               // x=type, y=shadowType, z=shadowMapIndex, w=shadowStrength
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;             // x=intensity, y=range, z=falloff, w=spotAngle
    vec4 shadow;             // x=bias, y=normalBias
    vec4 cascadeSplits;      // view-space split distances
    mat4 lightMatrices[4];   // directional cascades / spot uses [0]
};

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;

uniform vec4 u_baseColor;
uniform sampler2D u_baseColorTex;
uniform int u_useBaseColorTex;

uniform float u_metallic;
uniform float u_roughness;
uniform sampler2D u_metallicRoughnessTex;
uniform int u_useMetallicRoughnessTex;

uniform sampler2D u_normalTex;
uniform int u_useNormalTex;
uniform float u_normalScale;

uniform sampler2D u_emissiveTex;
uniform int u_useEmissiveTex;
uniform vec3 u_emissiveColor;
uniform float u_emissiveStrength;

uniform sampler2D u_occlusionTex;
uniform int u_useOcclusionTex;
uniform float u_aoStrength;

uniform samplerCube u_envMap;
uniform int u_useEnvMap;
uniform float u_envStrength;

uniform vec2 u_uvScale;
uniform vec2 u_uvOffset;

uniform vec3 u_viewPos;
uniform mat4 u_view;
uniform int u_receiveShadows;
uniform int u_debugShadows; // 0=off,1=visibility,2=cascade index,3=proj bounds
uniform int u_useAlphaClip;
uniform float u_alphaCutoff;

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;      // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

uniform sampler2DShadow u_shadowMaps2D[MAX_SHADOW_MAPS_2D];
uniform samplerCubeShadow u_shadowMapsCube[MAX_SHADOW_MAPS_CUBE];

float sampleShadow2D(int shadowType, int mapIndex, vec4 lightSpacePos, float bias) {
    if(mapIndex < 0 || mapIndex >= MAX_SHADOW_MAPS_2D) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    if(projCoords.z > 1.0 || projCoords.z < 0.0) return 1.0;

    vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMaps2D[mapIndex], 0));
    float shadow = 0.0;
    int samples = 0;

    if(shadowType == 0){
        return texture(u_shadowMaps2D[mapIndex], vec3(projCoords.xy, projCoords.z - bias));
    }else if(shadowType == 1){
        for(int x = -1; x <= 1; ++x){
            for(int y = -1; y <= 1; ++y){
                shadow += texture(u_shadowMaps2D[mapIndex], vec3(projCoords.xy + vec2(x,y) * texelSize, projCoords.z - bias));
                samples++;
            }
        }
    }else{
        for(int x = -2; x <= 2; ++x){
            for(int y = -2; y <= 2; ++y){
                shadow += texture(u_shadowMaps2D[mapIndex], vec3(projCoords.xy + vec2(x,y) * texelSize, projCoords.z - bias));
                samples++;
            }
        }
    }

    return shadow / float(samples);
}

float sampleShadowCube(int shadowType, int mapIndex, vec3 fragPos, vec3 lightPos, float farPlane, float bias) {
    if(mapIndex < 0 || mapIndex >= MAX_SHADOW_MAPS_CUBE) return 1.0;

    vec3 toLight = fragPos - lightPos;
    float currentDepth = length(toLight);
    float depth = currentDepth / farPlane;
    vec3 dir = normalize(toLight);

    if(shadowType == 0){
        return texture(u_shadowMapsCube[mapIndex], vec4(dir, depth - bias));
    }else if(shadowType == 1){
        float shadow = 0.0;
        vec3 offsets[4] = vec3[4](
            vec3( 0.01, 0.01, 0.01),
            vec3(-0.01, 0.01,-0.01),
            vec3( 0.01,-0.01, 0.01),
            vec3(-0.01,-0.01,-0.01)
        );
        for(int i = 0; i < 4; ++i){
            shadow += texture(u_shadowMapsCube[mapIndex], vec4(normalize(dir + offsets[i]), depth - bias));
        }
        return shadow * 0.25;
    }else{
        float shadow = 0.0;
        vec3 offsets[8] = vec3[8](
            vec3( 0.01, 0.01, 0.01), vec3(-0.01, 0.01, 0.01),
            vec3( 0.01,-0.01, 0.01), vec3(-0.01,-0.01, 0.01),
            vec3( 0.01, 0.01,-0.01), vec3(-0.01, 0.01,-0.01),
            vec3( 0.01,-0.01,-0.01), vec3(-0.01,-0.01,-0.01)
        );
        for(int i = 0; i < 8; ++i){
            shadow += texture(u_shadowMapsCube[mapIndex], vec4(normalize(dir + offsets[i]), depth - bias));
        }
        return shadow * 0.125;
    }
}

vec3 getNormal(vec2 uv){
    vec3 N = normalize(v_normal);
    if(u_useNormalTex == 0){
        return N;
    }

    vec3 mapN = texture(u_normalTex, uv).xyz * 2.0 - 1.0;
    mapN.xy *= u_normalScale;

    vec3 dp1 = dFdx(v_fragPos);
    vec3 dp2 = dFdy(v_fragPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 B = normalize(-dp1 * duv2.x + dp2 * duv1.x);
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * mapN);
}

float DistributionGGX(vec3 N, vec3 H, float roughness){
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness){
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness){
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0){
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec2 uv = v_uv * u_uvScale + u_uvOffset;
    vec4 baseTex = (u_useBaseColorTex != 0) ? texture(u_baseColorTex, uv) : vec4(1.0);
    vec4 baseColor = u_baseColor * baseTex * v_color;

    if(u_useAlphaClip != 0 && baseColor.a < u_alphaCutoff){
        discard;
    }

    float metallic = u_metallic;
    float roughness = clamp(u_roughness, 0.04, 1.0);
    if(u_useMetallicRoughnessTex != 0){
        vec4 mr = texture(u_metallicRoughnessTex, uv);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
    }

    float ao = 1.0;
    if(u_useOcclusionTex != 0){
        float occl = texture(u_occlusionTex, uv).r;
        ao = mix(1.0, occl, clamp(u_aoStrength, 0.0, 1.0));
    }

    vec3 N = getNormal(uv);
    vec3 V = normalize(u_viewPos - v_fragPos);

    vec3 albedo = baseColor.rgb;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    float debugVisibility = 1.0;
    int lightCount = int(u_lightHeader.x + 0.5);
    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        Light light = u_lights[i];
        int lightType = int(light.meta.x + 0.5);

        vec3 L = vec3(0.0);
        float attenuation = 1.0;
        float spotFactor = 1.0;

        if (lightType == 0) {
            vec3 toLight = light.position.xyz - v_fragPos;
            float distance = length(toLight);
            L = normalize(toLight);
            if(distance < light.params.y){
                float range = max(light.params.y, 0.001);
                float d = clamp(distance / range, 0.0, 1.0);
                float falloff = max(light.params.z, 0.001);
                attenuation = pow(1.0 - d, falloff);
            }else{
                attenuation = 0.0;
            }
        } else if (lightType == 1) {
            L = normalize(-light.direction.xyz);
        } else if (lightType == 2) {
            vec3 toLight = light.position.xyz - v_fragPos;
            float distance = length(toLight);
            L = normalize(toLight);
            if(distance < light.params.y){
                float theta = degrees(acos(dot(-L, normalize(light.direction.xyz))));
                if(theta < light.params.w){
                    float range = max(light.params.y, 0.001);
                    float d = clamp(distance / range, 0.0, 1.0);
                    float falloff = max(light.params.z, 0.001);
                    attenuation = pow(1.0 - d, falloff);
                }else{
                    attenuation = 0.0;
                }
            }else{
                attenuation = 0.0;
            }
        }

        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        if(NdotL <= 0.0){
            continue;
        }

        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
        vec3 specular = numerator / denom;

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

        vec3 radiance = light.color.rgb * light.params.x * attenuation * spotFactor;
        vec3 lightContribution = (kD * albedo / PI + specular) * radiance * NdotL;

        if(u_receiveShadows != 0 && light.meta.z >= 0.0){
            float bias = light.shadow.x + light.shadow.y * (1.0 - max(dot(N, normalize(light.direction.xyz)), 0.0));
            float visibility = 1.0;
            int lType = int(light.meta.x + 0.5);
            int sType = int(light.meta.y + 0.5);
            int baseIndex = int(light.meta.z + 0.5);
            int cascadeCount = 1;
            int cascadeIndex = 0;

            if(lType == 0){
                visibility = sampleShadowCube(sType, baseIndex, v_fragPos, light.position.xyz, light.params.y, bias);
            }else{
                cascadeCount = int(light.shadow.z + 0.5);
                float viewDepth = -(u_view * vec4(v_fragPos, 1.0)).z;
                if(cascadeCount > 1){
                    if(viewDepth > light.cascadeSplits.x) cascadeIndex = 1;
                    if(viewDepth > light.cascadeSplits.y) cascadeIndex = 2;
                    if(viewDepth > light.cascadeSplits.z) cascadeIndex = 3;
                    cascadeIndex = clamp(cascadeIndex, 0, cascadeCount - 1);
                }
                vec4 lightSpacePos = light.lightMatrices[cascadeIndex] * vec4(v_fragPos, 1.0);
                if(u_debugShadows == 3){
                    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
                    projCoords = projCoords * 0.5 + 0.5;
                    bool outBounds = (projCoords.x < 0.0 || projCoords.x > 1.0 ||
                                      projCoords.y < 0.0 || projCoords.y > 1.0 ||
                                      projCoords.z < 0.0 || projCoords.z > 1.0);
                    FragColor = outBounds ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(vec3(projCoords.z), 1.0);
                    return;
                }
                visibility = sampleShadow2D(sType, baseIndex + cascadeIndex, lightSpacePos, bias);
            }

            visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
            lightContribution *= visibility;
            debugVisibility = min(debugVisibility, visibility);
            if(u_debugShadows == 2 && lType != 0){
                float t = (cascadeCount > 1) ? (float(cascadeIndex) / float(max(cascadeCount - 1, 1))) : 0.0;
                FragColor = vec4(vec3(t), 1.0);
                return;
            }
        }

        Lo += lightContribution;
    }

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 envSpec = vec3(0.0);
    if(u_useEnvMap != 0){
        vec3 R = reflect(-V, N);
        float NdotV = max(dot(N, V), 0.0);
        vec3 Fenv = FresnelSchlick(NdotV, F0);
        vec3 envSample = texture(u_envMap, R).rgb;
        envSpec = envSample * Fenv * u_envStrength * (1.0 - roughness);
    }
    vec3 emissive = u_emissiveColor * u_emissiveStrength;
    if(u_useEmissiveTex != 0){
        emissive *= texture(u_emissiveTex, uv).rgb;
    }

    vec3 color = ambient + Lo + envSpec + emissive;
    if(u_debugShadows == 1){
        FragColor = vec4(vec3(debugVisibility), 1.0);
    }else if(u_debugShadows == 2){
        FragColor = vec4(1.0);
    }else if(u_debugShadows == 3){
        FragColor = vec4(1.0);
    }else{
        FragColor = vec4(color, baseColor.a);
    }
}
