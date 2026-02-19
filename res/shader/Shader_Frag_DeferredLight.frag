#version 410 core

#define MAX_LIGHTS 16
#define MAX_SHADOW_MAPS_2D 24
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

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D gAlbedo;
uniform sampler2D gNormal;
uniform sampler2D gPosition;
uniform vec3 u_viewPos;
uniform mat4 u_cameraView;
uniform samplerCube u_envMap;
uniform int u_useEnvMap;
uniform float u_envStrength;
uniform sampler2DShadow u_shadowMaps2D[MAX_SHADOW_MAPS_2D];
uniform samplerCubeShadow u_shadowMapsCube[MAX_SHADOW_MAPS_CUBE];

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;      // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

vec3 safeNormalize(vec3 v){
    float lenV = length(v);
    return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, -1.0, 0.0);
}

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
    float cubeTexel = 1.0 / float(max(textureSize(u_shadowMapsCube[mapIndex], 0).x, 1));

    if(shadowType == 0){
        return texture(u_shadowMapsCube[mapIndex], vec4(dir, depth - bias));
    }else if(shadowType == 1){
        float shadow = 0.0;
        float sampleRadius = cubeTexel * 1.5;
        vec3 offsets[4] = vec3[4](
            vec3( 1.0, 1.0, 1.0),
            vec3(-1.0, 1.0,-1.0),
            vec3( 1.0,-1.0, 1.0),
            vec3(-1.0,-1.0,-1.0)
        );
        for(int i = 0; i < 4; ++i){
            shadow += texture(u_shadowMapsCube[mapIndex], vec4(normalize(dir + offsets[i] * sampleRadius), depth - bias));
        }
        return shadow * 0.25;
    }else{
        float shadow = 0.0;
        float sampleRadius = cubeTexel * 2.5;
        vec3 offsets[8] = vec3[8](
            vec3( 1.0, 1.0, 1.0), vec3(-1.0, 1.0, 1.0),
            vec3( 1.0,-1.0, 1.0), vec3(-1.0,-1.0, 1.0),
            vec3( 1.0, 1.0,-1.0), vec3(-1.0, 1.0,-1.0),
            vec3( 1.0,-1.0,-1.0), vec3(-1.0,-1.0,-1.0)
        );
        for(int i = 0; i < 8; ++i){
            shadow += texture(u_shadowMapsCube[mapIndex], vec4(normalize(dir + offsets[i] * sampleRadius), depth - bias));
        }
        return shadow * 0.125;
    }
}

float computeShadowBias(Light light, vec3 normal, vec3 fragPos){
    int lightType = int(light.meta.x + 0.5);
    vec3 biasDir = (lightType == 1)
        ? safeNormalize(-light.direction.xyz)
        : safeNormalize(light.position.xyz - fragPos);
    float NdotL = max(dot(normal, biasDir), 0.0);
    return light.shadow.x + light.shadow.y * (1.0 - NdotL);
}

float sampleShadowForLight(Light light, vec3 normal, vec3 fragPos){
    if(light.meta.z < 0.0){
        return 1.0;
    }

    int lightType = int(light.meta.x + 0.5);
    int shadowType = int(light.meta.y + 0.5);
    int baseIndex = int(light.meta.z + 0.5);
    vec3 biasDir = (lightType == 1)
        ? safeNormalize(-light.direction.xyz)
        : safeNormalize(light.position.xyz - fragPos);
    float NdotL = max(dot(normal, biasDir), 0.0);
    float slope = 1.0 - NdotL;

    float bias = max(light.shadow.x, 0.0002) * (1.0 + slope * 2.0);
    bias = max(bias, computeShadowBias(light, normal, fragPos) * 0.85);

    float normalOffset = max(light.shadow.y, 0.0) * (0.35 + slope * 1.65);
    vec3 receiverPos = fragPos + normal * normalOffset;

    if(lightType == 0){
        return sampleShadowCube(
            shadowType,
            baseIndex,
            receiverPos,
            light.position.xyz,
            max(light.cascadeSplits.x, 0.1),
            bias
        );
    }

    int cascadeCount = clamp(int(light.shadow.z + 0.5), 1, 4);
    int cascadeIndex = 0;
    float viewDepth = -(u_cameraView * vec4(receiverPos, 1.0)).z;
    if(cascadeCount > 1){
        if(viewDepth > light.cascadeSplits.x) cascadeIndex = 1;
        if(viewDepth > light.cascadeSplits.y) cascadeIndex = 2;
        if(viewDepth > light.cascadeSplits.z) cascadeIndex = 3;
        cascadeIndex = clamp(cascadeIndex, 0, cascadeCount - 1);
    }

    if(lightType == 1){
        receiverPos += normal * max(light.shadow.y, 0.0) * 0.75;
        viewDepth = -(u_cameraView * vec4(receiverPos, 1.0)).z;
        cascadeIndex = 0;
        if(cascadeCount > 1){
            if(viewDepth > light.cascadeSplits.x) cascadeIndex = 1;
            if(viewDepth > light.cascadeSplits.y) cascadeIndex = 2;
            if(viewDepth > light.cascadeSplits.z) cascadeIndex = 3;
            cascadeIndex = clamp(cascadeIndex, 0, cascadeCount - 1);
        }

        float cascadeScale = 1.0 + float(cascadeIndex) * 0.35;
        bias *= cascadeScale;
        bias = max(bias, 0.0045 + float(cascadeIndex) * 0.0012);
    }

    vec4 lightSpacePos = light.lightMatrices[cascadeIndex] * vec4(receiverPos, 1.0);
    float visibility = sampleShadow2D(shadowType, baseIndex + cascadeIndex, lightSpacePos, bias);

    if(lightType == 1 && cascadeCount > 1 && cascadeIndex < (cascadeCount - 1)){
        float splitDist = light.cascadeSplits[cascadeIndex];
        float blendRange = max(splitDist * 0.10, 3.0);
        float blendT = clamp((viewDepth - (splitDist - blendRange)) / blendRange, 0.0, 1.0);
        if(blendT > 0.0){
            vec4 nextLightSpacePos = light.lightMatrices[cascadeIndex + 1] * vec4(receiverPos, 1.0);
            float nextVisibility = sampleShadow2D(shadowType, baseIndex + cascadeIndex + 1, nextLightSpacePos, bias);
            visibility = mix(visibility, nextVisibility, blendT);
        }
    }

    return visibility;
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

void main(){
    vec4 albedoRough = texture(gAlbedo, v_uv);
    vec4 normalMetal = texture(gNormal, v_uv);
    vec4 positionAo = texture(gPosition, v_uv);

    vec3 albedo = albedoRough.rgb;
    float roughness = clamp(albedoRough.a, 0.04, 1.0);
    float packedMode = normalMetal.a;
    float ao = clamp(positionAo.a, 0.0, 1.0);

    vec3 fragPos = positionAo.rgb;
    vec3 N = safeNormalize(normalMetal.rgb);
    vec3 V = safeNormalize(u_viewPos - fragPos);

    bool legacyUnlit = (packedMode < -0.75);
    bool legacyLit = (packedMode < 0.0 && !legacyUnlit);

    if(legacyUnlit){
        FragColor = vec4(albedo, 1.0);
        return;
    }

    if(legacyLit){
        vec3 LoLegacy = vec3(0.0);
        int lightCount = int(u_lightHeader.x + 0.5);
        for(int i = 0; i < lightCount && i < MAX_LIGHTS; ++i){
            Light light = u_lights[i];
            int lightType = int(light.meta.x + 0.5);

            vec3 L = vec3(0.0);
            float attenuation = 1.0;

            if(lightType == 0){
                vec3 toLight = light.position.xyz - fragPos;
                float distance = length(toLight);
                L = safeNormalize(toLight);
                if(distance < light.params.y){
                    float range = max(light.params.y, 0.001);
                    float d = clamp(distance / range, 0.0, 1.0);
                    float falloff = clamp(light.params.z, 0.001, 3.0);
                    attenuation = pow(1.0 - d, falloff);
                }else{
                    attenuation = 0.0;
                }
            }else if(lightType == 1){
                L = safeNormalize(-light.direction.xyz);
            }else if(lightType == 2){
                vec3 toLight = light.position.xyz - fragPos;
                float distance = length(toLight);
                L = safeNormalize(toLight);
                if(distance < light.params.y){
                    float cosTheta = clamp(dot(-L, safeNormalize(light.direction.xyz)), -1.0, 1.0);
                    float theta = degrees(acos(cosTheta));
                    if(theta < light.params.w){
                        float range = max(light.params.y, 0.001);
                        float d = clamp(distance / range, 0.0, 1.0);
                        float falloff = clamp(light.params.z, 0.001, 3.0);
                        attenuation = pow(1.0 - d, falloff);
                    }else{
                        attenuation = 0.0;
                    }
                }else{
                    attenuation = 0.0;
                }
            }

            float visibility = 1.0;
            if(light.meta.z >= 0.0){
                visibility = sampleShadowForLight(light, N, fragPos);
                visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
            }

            float NdotL = max(dot(N, L), 0.0);
            if(NdotL <= 0.0){
                continue;
            }

            vec3 diffuse = NdotL * light.color.rgb * albedo * attenuation * visibility;
            vec3 reflectDir = reflect(-L, N);
            float spec = pow(max(dot(V, reflectDir), 0.0), 32.0);
            vec3 specular = 0.2 * spec * light.color.rgb * attenuation * visibility;

            LoLegacy += (diffuse + specular) * light.params.x;
        }

        vec3 colorLegacy = vec3(0.3) * albedo + LoLegacy;
        FragColor = vec4(colorLegacy, 1.0);
        return;
    }

    float metallic = clamp(packedMode, 0.0, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    int lightCount = int(u_lightHeader.x + 0.5);
    for(int i = 0; i < lightCount && i < MAX_LIGHTS; ++i){
        Light light = u_lights[i];
        int lightType = int(light.meta.x + 0.5);

        vec3 L = vec3(0.0);
        float attenuation = 1.0;

        if(lightType == 0){
            vec3 toLight = light.position.xyz - fragPos;
            float distance = length(toLight);
            L = safeNormalize(toLight);
            if(distance < light.params.y){
                float range = max(light.params.y, 0.001);
                float d = clamp(distance / range, 0.0, 1.0);
                float falloff = clamp(light.params.z, 0.001, 3.0);
                attenuation = pow(1.0 - d, falloff);
            }else{
                attenuation = 0.0;
            }
        }else if(lightType == 1){
            L = safeNormalize(-light.direction.xyz);
        }else if(lightType == 2){
            vec3 toLight = light.position.xyz - fragPos;
            float distance = length(toLight);
            L = safeNormalize(toLight);
            if(distance < light.params.y){
                float cosTheta = clamp(dot(-L, safeNormalize(light.direction.xyz)), -1.0, 1.0);
                float theta = degrees(acos(cosTheta));
                if(theta < light.params.w){
                    float range = max(light.params.y, 0.001);
                    float d = clamp(distance / range, 0.0, 1.0);
                    float falloff = clamp(light.params.z, 0.001, 3.0);
                    attenuation = pow(1.0 - d, falloff);
                }else{
                    attenuation = 0.0;
                }
            }else{
                attenuation = 0.0;
            }
        }

        float visibility = 1.0;
        if(light.meta.z >= 0.0){
            visibility = sampleShadowForLight(light, N, fragPos);
            visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
        }

        vec3 H = safeNormalize(V + L);
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

        vec3 radiance = light.color.rgb * light.params.x * attenuation * visibility;
        vec3 lightContribution = (kD * albedo / PI + specular) * radiance * NdotL;
        Lo += lightContribution;
    }

    vec3 ambient = vec3(0.10) * albedo * ao;
    vec3 envSpec = vec3(0.0);
    if(u_useEnvMap != 0){
        vec3 R = reflect(-V, N);
        float NdotV = max(dot(N, V), 0.0);
        vec3 Fenv = FresnelSchlick(NdotV, F0);
        vec3 envSample = texture(u_envMap, R).rgb;
        envSpec = envSample * Fenv * u_envStrength * (1.0 - roughness);
    }

    vec3 color = ambient + Lo + envSpec;
    FragColor = vec4(color, 1.0);
}
