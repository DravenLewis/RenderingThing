#version 410 core

#define MAX_LIGHTS 16
#define MAX_SHADOW_MAPS_2D 16
#define MAX_SHADOW_MAPS_CUBE 2
#define PI 3.14159265359

struct Light {
    vec4 meta;               // x=type, y=shadowType, z=shadowMapIndex, w=shadowStrength
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;             // x=intensity, y=range, z=falloff, w=spotAngle
    vec4 shadow;             // x=bias, y=normalBias, z=cascadeCount, w=debugMode
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
uniform int u_debugShadows; // 0=off,1=visibility,2=cascade index,3=proj bounds
uniform int u_debugSelectedLightIndex;
uniform float u_shadowReceiverNormalBlend;
uniform sampler2D u_shadowMaps2D[MAX_SHADOW_MAPS_2D];
uniform samplerCubeShadow u_shadowMapsCube[MAX_SHADOW_MAPS_CUBE];

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;      // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

vec3 safeNormalize(vec3 v){
    float lenV = length(v);
    return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, -1.0, 0.0);
}

vec3 getShadowDirection(Light light, vec3 fragPos){
    int lightType = int(light.meta.x + 0.5);
    return (lightType == 1)
        ? safeNormalize(-light.direction.xyz)
        : safeNormalize(light.position.xyz - fragPos);
}

float hash13(vec3 p){
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

mat2 rotate2D(float angle){
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c);
}

float computeRangeAttenuation(float distanceToLight, float range, float falloff){
    float safeRange = max(range, 0.001);
    float d = clamp(distanceToLight / safeRange, 0.0, 1.0);
    float edge = 1.0 - d;
    // Smooth range rolloff to avoid visible contouring.
    float smoothEdge = edge * edge * (3.0 - 2.0 * edge);
    return pow(smoothEdge, clamp(falloff, 0.001, 3.0));
}

float computeSpotConeAttenuation(vec3 lightToFragDir, vec3 spotForwardDir, float outerAngleDeg){
    float clampedOuter = clamp(outerAngleDeg, 1.0, 89.0);
    float innerAngle = max(clampedOuter * 0.85, clampedOuter - 6.0);
    float cosOuter = cos(radians(clampedOuter));
    float cosInner = cos(radians(clamp(innerAngle, 0.5, 88.5)));
    float cosTheta = clamp(dot(-lightToFragDir, safeNormalize(spotForwardDir)), -1.0, 1.0);
    return smoothstep(cosOuter, cosInner, cosTheta);
}

float compareShadowDepth2D(int mapIndex, vec2 uv, float compareDepth){
    float storedDepth = texture(u_shadowMaps2D[mapIndex], uv).r;
    return (compareDepth <= storedDepth) ? 1.0 : 0.0;
}

float sampleShadow2D(int shadowType, int mapIndex, vec4 lightSpacePos, float bias, float filterScale, vec3 receiverPos, float receiverGradScale) {
    if(mapIndex < 0 || mapIndex >= MAX_SHADOW_MAPS_2D) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    if(projCoords.z > 1.001 || projCoords.z < -0.001) return 1.0;
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 1.0;

    vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMaps2D[mapIndex], 0));
    float clampedFilterScale = max(filterScale, 0.5);
    vec2 pcfTexel = texelSize * clampedFilterScale;
    float pcfBias = 0.0;
    if(shadowType == 1){
        pcfBias = max(0.00012, max(texelSize.x, texelSize.y) * clampedFilterScale * 0.16);
    }else if(shadowType == 2){
        pcfBias = max(0.00022, max(texelSize.x, texelSize.y) * clampedFilterScale * 0.24);
    }
    // Receiver-plane depth gradient keeps PCF taps on the same surface at grazing angles.
    vec2 uvDx = dFdx(projCoords.xy);
    vec2 uvDy = dFdy(projCoords.xy);
    float zDx = dFdx(projCoords.z);
    float zDy = dFdy(projCoords.z);
    float det = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    vec2 receiverDepthGrad = vec2(0.0);
    if(abs(det) > 1e-8){
        receiverDepthGrad = vec2(
            (zDx * uvDy.y - zDy * uvDx.y) / det,
            (zDy * uvDx.x - zDx * uvDy.x) / det
        );
    }
    receiverDepthGrad *= clamp(receiverGradScale, 0.0, 1.0);
    float receiverSlope = length(receiverDepthGrad);
    if(receiverSlope > 10.0){
        receiverDepthGrad *= (10.0 / receiverSlope);
        receiverSlope = 10.0;
    }
    float receiverPlaneBias = 0.0;
    if(shadowType != 0){
        float kernelRadius = (shadowType == 1) ? 1.00 : 1.55;
        float texelRadius = kernelRadius * clampedFilterScale * max(texelSize.x, texelSize.y);
        float rpScale = (shadowType == 1) ? 0.36 : 0.52;
        float rpCap = (shadowType == 1) ? 0.00055 : 0.00085;
        receiverPlaneBias = clamp(receiverSlope * texelRadius * rpScale, 0.0, rpCap);
    }
    float compareDepth = clamp(projCoords.z - (bias + pcfBias + receiverPlaneBias), 0.0, 1.0);
    vec2 uvMin = texelSize * 1.5;
    vec2 uvMax = vec2(1.0) - uvMin;
    float rotation = hash13(floor(receiverPos * 32.0) * 0.754877666 + vec3(float(mapIndex), float(shadowType), 0.0)) * 6.28318530718;
    mat2 kernelRot = rotate2D(rotation);

    if(shadowType == 0){
        return compareShadowDepth2D(mapIndex, clamp(projCoords.xy, uvMin, uvMax), compareDepth);
    }

    float shadow = 0.0;
    float weightSum = 0.0;

    if(shadowType == 1){
        const int tapCount = 8;
        vec2 kernel[tapCount] = vec2[tapCount](
            vec2(-0.326, -0.406),
            vec2(-0.840, -0.074),
            vec2(-0.696,  0.457),
            vec2(-0.203,  0.621),
            vec2( 0.962, -0.195),
            vec2( 0.473, -0.480),
            vec2( 0.519,  0.767),
            vec2( 0.185, -0.893)
        );
        for(int i = 0; i < tapCount; ++i){
            vec2 tapOffset = (kernelRot * kernel[i]) * pcfTexel * 1.00;
            vec2 sampleUv = clamp(projCoords.xy + tapOffset, uvMin, uvMax);
            float weight = 1.0 - smoothstep(0.75, 1.10, length(kernel[i]));
            float receiverOffset = clamp(dot(receiverDepthGrad, tapOffset), -0.0012 * clampedFilterScale, 0.0012 * clampedFilterScale);
            float compareDepthTap = clamp(compareDepth + receiverOffset, 0.0, 1.0);
            shadow += compareShadowDepth2D(mapIndex, sampleUv, compareDepthTap) * weight;
            weightSum += weight;
        }
    }else{
        const int tapCount = 16;
        vec2 kernel[tapCount] = vec2[tapCount](
            vec2(-0.942, -0.399), vec2( 0.945, -0.384),
            vec2(-0.807,  0.577), vec2( 0.811,  0.566),
            vec2(-0.316, -0.949), vec2( 0.324, -0.946),
            vec2(-0.249,  0.968), vec2( 0.231,  0.973),
            vec2(-0.619, -0.153), vec2( 0.620, -0.140),
            vec2(-0.531,  0.145), vec2( 0.541,  0.124),
            vec2(-0.093, -0.553), vec2( 0.105, -0.548),
            vec2(-0.087,  0.553), vec2( 0.091,  0.560)
        );
        for(int i = 0; i < tapCount; ++i){
            vec2 tapOffset = (kernelRot * kernel[i]) * pcfTexel * 1.55;
            vec2 sampleUv = clamp(projCoords.xy + tapOffset, uvMin, uvMax);
            float weight = 1.0 - smoothstep(0.70, 1.15, length(kernel[i]));
            float receiverOffset = clamp(dot(receiverDepthGrad, tapOffset), -0.0020 * clampedFilterScale, 0.0020 * clampedFilterScale);
            float compareDepthTap = clamp(compareDepth + receiverOffset, 0.0, 1.0);
            shadow += compareShadowDepth2D(mapIndex, sampleUv, compareDepthTap) * weight;
            weightSum += weight;
        }
    }

    float visibility = shadow / max(weightSum, 0.0001);
    float edge = min(min(projCoords.x, projCoords.y), min(1.0 - projCoords.x, 1.0 - projCoords.y));
    float edgeFade = smoothstep(0.0, max(texelSize.x, texelSize.y) * (8.0 * clampedFilterScale), edge);
    return mix(1.0, visibility, edgeFade);
}

float sampleShadowCube(int shadowType, int mapIndex, vec3 fragPos, vec3 lightPos, float farPlane, float bias) {
    if(mapIndex < 0 || mapIndex >= MAX_SHADOW_MAPS_CUBE) return 1.0;

    float safeFarPlane = max(farPlane, 0.1);
    vec3 toLight = fragPos - lightPos;
    float currentDepth = length(toLight);
    if(currentDepth >= safeFarPlane){
        return 1.0;
    }
    float depth = currentDepth / safeFarPlane;
    vec3 dir = safeNormalize(toLight);
    float cubeTexel = 1.0 / float(max(textureSize(u_shadowMapsCube[mapIndex], 0).x, 1));
    float depth01 = clamp(depth, 0.0, 1.0);
    float cubeBias = cubeTexel * mix(1.8, 4.0, depth01);
    if(shadowType == 1){
        cubeBias *= 1.15;
    }else if(shadowType == 2){
        cubeBias *= 1.35;
    }
    float compareDepth = clamp(depth - (bias + cubeBias), 0.0, 1.0);

    if(shadowType == 0){
        return texture(u_shadowMapsCube[mapIndex], vec4(dir, compareDepth));
    }

    vec3 up = (abs(dir.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = safeNormalize(cross(up, dir));
    vec3 bitangent = cross(dir, tangent);
    float rotation = hash13(fragPos * 0.37 + lightPos * 0.11) * 6.28318530718;
    mat2 kernelRot = rotate2D(rotation);
    float distScale = mix(0.85, 2.0, clamp(depth, 0.0, 1.0));
    float sampleRadius = cubeTexel * ((shadowType == 1) ? 1.4 : 2.4) * distScale;
    float shadow = 0.0;

    if(shadowType == 1){
        const int tapCount = 8;
        vec2 kernel[tapCount] = vec2[tapCount](
            vec2(-0.326, -0.406),
            vec2(-0.840, -0.074),
            vec2(-0.696,  0.457),
            vec2(-0.203,  0.621),
            vec2( 0.962, -0.195),
            vec2( 0.473, -0.480),
            vec2( 0.519,  0.767),
            vec2( 0.185, -0.893)
        );
        for(int i = 0; i < tapCount; ++i){
            vec2 o = kernelRot * (kernel[i] * sampleRadius);
            vec3 sampleDir = safeNormalize(dir + tangent * o.x + bitangent * o.y);
            shadow += texture(u_shadowMapsCube[mapIndex], vec4(sampleDir, compareDepth));
        }
        return shadow / float(tapCount);
    }

    const int tapCount = 16;
    vec2 kernel[tapCount] = vec2[tapCount](
        vec2(-0.942, -0.399), vec2( 0.945, -0.384),
        vec2(-0.807,  0.577), vec2( 0.811,  0.566),
        vec2(-0.316, -0.949), vec2( 0.324, -0.946),
        vec2(-0.249,  0.968), vec2( 0.231,  0.973),
        vec2(-0.619, -0.153), vec2( 0.620, -0.140),
        vec2(-0.531,  0.145), vec2( 0.541,  0.124),
        vec2(-0.093, -0.553), vec2( 0.105, -0.548),
        vec2(-0.087,  0.553), vec2( 0.091,  0.560)
    );
    for(int i = 0; i < tapCount; ++i){
        vec2 o = kernelRot * (kernel[i] * sampleRadius);
        vec3 sampleDir = safeNormalize(dir + tangent * o.x + bitangent * o.y);
        shadow += texture(u_shadowMapsCube[mapIndex], vec4(sampleDir, compareDepth));
    }
    return shadow / float(tapCount);
}

float computeShadowBias(Light light, vec3 normal, vec3 fragPos){
    int lightType = int(light.meta.x + 0.5);
    vec3 shadowDir = getShadowDirection(light, fragPos);
    float NdotL = max(dot(normal, shadowDir), 0.0);
    float slope = 1.0 - NdotL;
    float baseOffset = max(light.shadow.x, 0.0);
    float normalOffset = max(light.shadow.y, 0.0);
    if(lightType == 1){
        float cascadeCount = max(light.shadow.z, 1.0);
        float singleCascadeScale = (cascadeCount <= 1.5) ? 1.45 : 1.0;
        float baseBias = baseOffset * 0.010 * singleCascadeScale;
        float slopeBias = normalOffset * (0.0014 + slope * 0.0120) * singleCascadeScale;
        float minBias = (cascadeCount <= 1.5) ? 0.00005 : 0.00003;
        float maxBias = (cascadeCount <= 1.5) ? 0.00078 : 0.00046;
        return clamp(baseBias + slopeBias, minBias, maxBias);
    }else if(lightType == 2){
        float baseBias = baseOffset * 0.020;
        float slopeBias = normalOffset * (0.0030 + slope * 0.0160);
        return clamp(baseBias + slopeBias, 0.00006, 0.00060);
    }else{
        float baseBias = baseOffset * 0.060;
        float slopeBias = normalOffset * (0.0100 + slope * 0.0400);
        return clamp(baseBias + slopeBias, 0.00020, 0.00120);
    }
}

vec3 offsetShadowReceiver(Light light, vec3 normal, vec3 fragPos){
    int lightType = int(light.meta.x + 0.5);
    vec3 normalDir = safeNormalize(normal);
    float baseOffset = max(light.shadow.x, 0.0);
    float normalOffset = max(light.shadow.y, 0.0);
    if(lightType == 1){
        vec3 shadowDir = getShadowDirection(light, fragPos);
        float grazing = 1.0 - max(dot(normalDir, shadowDir), 0.0);
        float cascadeCount = max(light.shadow.z, 1.0);
        float singleCascadeScale = (cascadeCount <= 1.5) ? 1.35 : 1.0;
        float worldOffset = (normalOffset * (0.0045 + grazing * 0.0150) + baseOffset * 0.0018) * singleCascadeScale;
        float worldMax = (cascadeCount <= 1.5) ? 0.00058 : 0.00034;
        worldOffset = clamp(worldOffset, 0.0, worldMax);
        return fragPos + normalDir * worldOffset;
    }

    vec3 shadowDir = getShadowDirection(light, fragPos);
    float grazing = 1.0 - max(dot(normalDir, shadowDir), 0.0);

    if(lightType == 2){
        float worldOffset = normalOffset * (0.012 + grazing * 0.030) + baseOffset * 0.004;
        worldOffset = clamp(worldOffset, 0.0, 0.00120);
        return fragPos + normalDir * worldOffset;
    }

    float worldOffset = normalOffset * (0.050 + grazing * 0.140) + baseOffset * 0.015;
    worldOffset = clamp(worldOffset, 0.0, 0.00450);
    return fragPos + normalDir * worldOffset;
}

vec3 computeShadowNormal(vec3 shadingNormal, vec3 fragPos){
    vec3 baseN = safeNormalize(shadingNormal);
    vec3 dpdx = dFdx(fragPos);
    vec3 dpdy = dFdy(fragPos);
    vec3 geom = cross(dpdx, dpdy);
    float geomLen = length(geom);
    if(geomLen <= 1e-6){
        return baseN;
    }
    vec3 geomN = geom / geomLen;
    if(dot(geomN, baseN) < 0.0){
        geomN = -geomN;
    }
    // Blend with the shading normal to damp derivative-driven temporal shimmer.
    float normalBlend = clamp(u_shadowReceiverNormalBlend, 0.0, 1.0);
    return safeNormalize(mix(baseN, geomN, normalBlend));
}

float sampleShadowForLight(Light light, vec3 normal, vec3 fragPos){
    if(light.meta.z < 0.0){
        return 1.0;
    }

    int lightType = int(light.meta.x + 0.5);
    float receiverGradScale = 1.0;
    if(lightType == 1){
        receiverGradScale = (light.shadow.z <= 1.5) ? 0.42 : 0.24;
    }
    int shadowType = int(light.meta.y + 0.5);
    int baseIndex = int(light.meta.z + 0.5);
    float bias = computeShadowBias(light, normal, fragPos);
    vec3 receiverPos = offsetShadowReceiver(light, normal, fragPos);
    float filterScale = 1.0;

    if(lightType == 1 && shadowType != 0){
        float receiverNdotL = max(dot(normal, getShadowDirection(light, fragPos)), 0.0);
        float grazing = 1.0 - receiverNdotL;
        // Slightly tighten directional PCF at grazing angles where projection stretch
        // already softens edges and over-filtering looks blurry.
        float grazingFilterMin = (shadowType == 2) ? 0.55 : 0.70;
        filterScale = mix(1.0, grazingFilterMin, grazing);
    }

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
        for(int c = 0; c < 3; ++c){
            if(c >= (cascadeCount - 1)){
                break;
            }
            if(viewDepth > light.cascadeSplits[c]){
                cascadeIndex = c + 1;
            }else{
                break;
            }
        }
    }
    vec4 lightSpacePos = light.lightMatrices[cascadeIndex] * vec4(receiverPos, 1.0);
    float visibility = sampleShadow2D(shadowType, baseIndex + cascadeIndex, lightSpacePos, bias, filterScale, receiverPos, receiverGradScale);
    if(lightType == 1 && cascadeCount > 1 && cascadeIndex == (cascadeCount - 1) && shadowType != 0){
        float prevSplit = (cascadeIndex > 0) ? light.cascadeSplits[cascadeIndex - 1] : 0.0;
        float splitDist = light.cascadeSplits[cascadeIndex];
        float cascadeSpan = max(splitDist - prevSplit, 0.001);
        float localCascadeT = clamp((viewDepth - prevSplit) / cascadeSpan, 0.0, 1.0);
        float hardVisibility = sampleShadow2D(0, baseIndex + cascadeIndex, lightSpacePos, bias, 1.0, receiverPos, 0.0);
        float filterToHard = smoothstep(0.25, 0.95, localCascadeT);
        visibility = mix(visibility, hardVisibility, filterToHard);
    }

    if(lightType == 1 && cascadeCount > 1 && cascadeIndex < (cascadeCount - 1)){
        float splitDist = light.cascadeSplits[cascadeIndex];
        float prevSplit = (cascadeIndex > 0) ? light.cascadeSplits[cascadeIndex - 1] : 0.0;
        float cascadeSpan = max(splitDist - prevSplit, 0.001);
        float maxBlendRange = max(16.0, splitDist * 0.40);
        float blendRange = clamp(cascadeSpan * 0.55, 1.5, maxBlendRange);
        float blendStart = splitDist - blendRange;
        float blendT = clamp((viewDepth - blendStart) / blendRange, 0.0, 1.0);
        if(blendT > 0.0){
            vec4 nextLightSpacePos = light.lightMatrices[cascadeIndex + 1] * vec4(receiverPos, 1.0);
            float nextVisibility = sampleShadow2D(shadowType, baseIndex + cascadeIndex + 1, nextLightSpacePos, bias, filterScale, receiverPos, receiverGradScale);
            visibility = mix(visibility, nextVisibility, blendT);
        }
    }

    if(lightType == 1 && cascadeCount > 1){
        float maxShadowDepth = light.cascadeSplits[cascadeCount - 1];
        if(maxShadowDepth > 0.0){
            // Fade out the last cascade edge so the far shadow cutoff is not a hard line.
            float fadeStart = maxShadowDepth * ((shadowType == 0) ? 0.985 : 0.94);
            float fadeWidth = max(maxShadowDepth - fadeStart, (shadowType == 0) ? 2.5 : 6.0);
            float farFade = clamp((viewDepth - fadeStart) / fadeWidth, 0.0, 1.0);
            visibility = mix(visibility, 1.0, farFade);
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

int resolveShadowDebugMode(int lightIndex, Light light){
    if(u_debugShadows > 0){
        return clamp(u_debugShadows, 0, 3);
    }
    if(lightIndex == u_debugSelectedLightIndex){
        return clamp(int(light.shadow.w + 0.5), 0, 3);
    }
    return 0;
}

vec3 debugLightColor(int lightIndex){
    float hue = fract(float(lightIndex) * 0.61803398875 + 0.19);
    vec3 t = abs(fract(vec3(hue) + vec3(0.0, 0.3333333, 0.6666667)) * 6.0 - 3.0) - 1.0;
    vec3 rgb = clamp(t, 0.0, 1.0);
    rgb = rgb * rgb * (3.0 - 2.0 * rgb);
    return mix(vec3(0.22), rgb, 0.90);
}

vec3 debugCascadeColor(int cascadeIndex){
    if(cascadeIndex <= 0) return vec3(0.22, 0.72, 1.0);
    if(cascadeIndex == 1) return vec3(0.28, 1.0, 0.35);
    if(cascadeIndex == 2) return vec3(1.0, 0.90, 0.25);
    return vec3(1.0, 0.35, 0.22);
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
    vec3 shadowNormal = computeShadowNormal(N, fragPos);

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
                attenuation = (distance < light.params.y)
                    ? computeRangeAttenuation(distance, light.params.y, light.params.z)
                    : 0.0;
            }else if(lightType == 1){
                L = safeNormalize(-light.direction.xyz);
            }else if(lightType == 2){
                vec3 toLight = light.position.xyz - fragPos;
                float distance = length(toLight);
                L = safeNormalize(toLight);
                if(distance < light.params.y){
                    float rangeAtten = computeRangeAttenuation(distance, light.params.y, light.params.z);
                    float coneAtten = computeSpotConeAttenuation(L, light.direction.xyz, light.params.w);
                    attenuation = rangeAtten * coneAtten;
                }else{
                    attenuation = 0.0;
                }
            }

            float visibility = 1.0;
            if(light.meta.z >= 0.0){
                visibility = sampleShadowForLight(light, shadowNormal, fragPos);
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
    vec3 debugColorAccum = vec3(0.0);
    float debugWeight = 0.0;
    int lightCount = int(u_lightHeader.x + 0.5);
    for(int i = 0; i < lightCount && i < MAX_LIGHTS; ++i){
        Light light = u_lights[i];
        int lightType = int(light.meta.x + 0.5);
        int debugModeForLight = resolveShadowDebugMode(i, light);

        vec3 L = vec3(0.0);
        float attenuation = 1.0;

        if(lightType == 0){
            vec3 toLight = light.position.xyz - fragPos;
            float distance = length(toLight);
            L = safeNormalize(toLight);
            attenuation = (distance < light.params.y)
                ? computeRangeAttenuation(distance, light.params.y, light.params.z)
                : 0.0;
        }else if(lightType == 1){
            L = safeNormalize(-light.direction.xyz);
        }else if(lightType == 2){
            vec3 toLight = light.position.xyz - fragPos;
            float distance = length(toLight);
            L = safeNormalize(toLight);
            if(distance < light.params.y){
                float rangeAtten = computeRangeAttenuation(distance, light.params.y, light.params.z);
                float coneAtten = computeSpotConeAttenuation(L, light.direction.xyz, light.params.w);
                attenuation = rangeAtten * coneAtten;
            }else{
                attenuation = 0.0;
            }
        }

        float visibility = 1.0;
        if(light.meta.z >= 0.0){
            visibility = sampleShadowForLight(light, shadowNormal, fragPos);
            visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
        }

        if(debugModeForLight == 1 && light.meta.z >= 0.0){
            vec3 debugColor = debugLightColor(i);
            debugColorAccum += mix(debugColor * 0.15, debugColor, visibility);
            debugWeight += 1.0;
        }else if((debugModeForLight == 2 || debugModeForLight == 3) &&
                 light.meta.z >= 0.0 && lightType != 0){
            vec3 shadowPosDebug = offsetShadowReceiver(light, shadowNormal, fragPos);
            int cascadeCountDebug = clamp(int(light.shadow.z + 0.5), 1, 4);
            int cascadeIndexDebug = 0;
            float viewDepthDebug = -(u_cameraView * vec4(shadowPosDebug, 1.0)).z;
            if(cascadeCountDebug > 1){
                for(int c = 0; c < 3; ++c){
                    if(c >= (cascadeCountDebug - 1)){
                        break;
                    }
                    if(viewDepthDebug > light.cascadeSplits[c]){
                        cascadeIndexDebug = c + 1;
                    }else{
                        break;
                    }
                }
            }

            if(debugModeForLight == 2){
                vec3 debugColor = mix(debugLightColor(i), debugCascadeColor(cascadeIndexDebug), 0.55);
                debugColorAccum += debugColor;
                debugWeight += 1.0;
            }else{
                vec4 lightSpacePosDebug = light.lightMatrices[cascadeIndexDebug] * vec4(shadowPosDebug, 1.0);
                vec3 projCoords = lightSpacePosDebug.xyz / lightSpacePosDebug.w;
                projCoords = projCoords * 0.5 + 0.5;
                bool outBounds = (projCoords.x < 0.0 || projCoords.x > 1.0 ||
                                  projCoords.y < 0.0 || projCoords.y > 1.0 ||
                                  projCoords.z < 0.0 || projCoords.z > 1.0);
                vec3 debugColor = debugLightColor(i);
                if(outBounds){
                    debugColor = mix(debugColor, vec3(1.0, 0.12, 0.08), 0.75);
                }else{
                    debugColor *= (0.4 + (0.6 * clamp(projCoords.z, 0.0, 1.0)));
                }
                debugColorAccum += debugColor;
                debugWeight += 1.0;
            }
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
    if(debugWeight > 0.0){
        FragColor = vec4(clamp(debugColorAccum / debugWeight, 0.0, 1.0), 1.0);
        return;
    }
    float dither = (hash13(vec3(gl_FragCoord.xy, fragPos.z * 0.03125)) - 0.5) / 255.0;
    color += vec3(dither);
    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}


