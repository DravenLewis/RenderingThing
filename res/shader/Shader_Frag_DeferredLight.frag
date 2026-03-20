#version 410 core

#define MAX_LIGHTS 128
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
uniform sampler2D gMaterial;
uniform sampler2D gSurface;
uniform sampler2D gDepth;
uniform isampler2D gTileLightData;
uniform sampler2D gSsaoRaw;
uniform sampler2D gSsao;
uniform sampler2D gGi;
uniform vec3 u_viewPos;
uniform mat4 u_cameraView;
uniform mat4 u_invProjection;
uniform mat4 u_invView;
uniform samplerCube u_envMap;
uniform samplerCube u_localProbe;
uniform int u_useEnvMap;
uniform int u_useLocalProbe;
uniform vec3 u_localProbeCenter;
uniform vec3 u_localProbeCaptureMin;
uniform vec3 u_localProbeCaptureMax;
uniform vec3 u_localProbeInfluenceMin;
uniform vec3 u_localProbeInfluenceMax;
uniform vec4 u_ambientColor;
uniform float u_ambientIntensity;
uniform int u_fogEnabled;
uniform vec4 u_fogColor;
uniform float u_fogStart;
uniform float u_fogStop;
uniform float u_fogEnd;
uniform int u_useSsao;
uniform int u_useGi;
uniform float u_ssaoIntensity;
uniform int u_ssaoDebugView;
uniform int u_lightPassMode; // 0=final composite, 1=direct diffuse prepass
uniform int u_useLightTiles;
uniform vec2 u_tileGrid;
uniform int u_tileSize;
uniform int u_debugShadows; // 0=off,1=visibility,2=cascade index,3=proj bounds
uniform int u_debugSelectedLightIndex;
uniform float u_shadowReceiverNormalBlend;
uniform sampler2D u_shadowMaps2D[MAX_SHADOW_MAPS_2D];
uniform samplerCubeShadow u_shadowMapsCube[MAX_SHADOW_MAPS_CUBE];

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;      // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

int resolveEffectiveShadowType(int lightIndex, Light light);

vec3 safeNormalize(vec3 v){
    float lenV = length(v);
    return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, -1.0, 0.0);
}

vec3 reconstructViewPosition(vec2 uv, float depth){
    vec4 clip = vec4((uv * 2.0) - 1.0, (depth * 2.0) - 1.0, 1.0);
    vec4 view = u_invProjection * clip;

    float invW = (abs(view.w) > 1e-6) ? (1.0 / view.w) : 0.0;
    return view.xyz * invW;
}

vec3 reconstructWorldPosition(vec2 uv, float depth){
    vec3 viewPos = reconstructViewPosition(uv, depth);
    return (u_invView * vec4(viewPos, 1.0)).xyz;
}

ivec2 getDeferredPixelCoord(vec2 uv){
    ivec2 depthSize = max(textureSize(gDepth, 0), ivec2(1));
    vec2 pixel = clamp(uv * vec2(depthSize), vec2(0.0), vec2(depthSize) - vec2(1.0));
    return ivec2(pixel);
}

ivec2 getDeferredTileCoord(vec2 uv){
    ivec2 tileGrid = max(ivec2(u_tileGrid + vec2(0.5)), ivec2(1));
    ivec2 tileCoord = getDeferredPixelCoord(uv) / max(u_tileSize, 1);
    return clamp(tileCoord, ivec2(0), tileGrid - ivec2(1));
}

int getDeferredTileIndex(vec2 uv){
    ivec2 tileGrid = max(ivec2(u_tileGrid + vec2(0.5)), ivec2(1));
    ivec2 tileCoord = getDeferredTileCoord(uv);
    return tileCoord.x + (tileCoord.y * tileGrid.x);
}

int getDeferredLightCount(int tileIndex, int fallbackLightCount){
    if(u_useLightTiles == 0){
        return fallbackLightCount;
    }
    return clamp(texelFetch(gTileLightData, ivec2(0, tileIndex), 0).r, 0, MAX_LIGHTS);
}

int getDeferredLightIndex(int tileIndex, int listIndex){
    if(u_useLightTiles == 0){
        return listIndex;
    }
    return texelFetch(gTileLightData, ivec2(listIndex + 1, tileIndex), 0).r;
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

    // Reconstructed depth is noisier than an explicit position buffer,
    // so keep this correction conservative.
    receiverDepthGrad *= clamp(receiverGradScale, 0.0, 1.0) * 0.35;

    float receiverSlope = length(receiverDepthGrad);
    if(receiverSlope > 2.5){
        receiverDepthGrad *= (2.5 / receiverSlope);
        receiverSlope = 2.5;
    }

    float receiverPlaneBias = 0.0;
    if(shadowType != 0){
        float kernelRadius = (shadowType == 1) ? 1.00 : 1.55;
        float texelRadius = kernelRadius * clampedFilterScale * max(texelSize.x, texelSize.y);

        float rpScale = (shadowType == 1) ? 0.18 : 0.24;
        float rpCap   = (shadowType == 1) ? 0.00028 : 0.00042;

        receiverPlaneBias = clamp(receiverSlope * texelRadius * rpScale, 0.0, rpCap);
    }
    float compareDepth = clamp(projCoords.z - (bias + pcfBias + receiverPlaneBias), 0.0, 1.0);
    vec2 uvMin = texelSize * 1.5;
    vec2 uvMax = vec2(1.0) - uvMin;

    vec2 pixel = floor(gl_FragCoord.xy);
    vec3 seed = vec3(
        pixel.x + float(mapIndex) * 17.0,
        pixel.y + float(shadowType) * 31.0,
        float(mapIndex) * 13.0 + float(shadowType) * 7.0
    );
    float rotation = hash13(seed) * 6.28318530718;
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
            float receiverOffset = clamp(dot(receiverDepthGrad, tapOffset), -0.00045 * clampedFilterScale, 0.00045 * clampedFilterScale);
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
            float receiverOffset = clamp(dot(receiverDepthGrad, tapOffset), -0.00075 * clampedFilterScale, 0.00075 * clampedFilterScale);
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

    // Old Kernal Rotation Code.
    float rotation = hash13(fragPos * 0.37 + lightPos * 0.11) * 6.28318530718;
    mat2 kernelRot = rotate2D(rotation);

    //vec3 seed = vec3(gl_FragCoord.xy, float(mapIndex));
    //float rotation = hash13(seed) * 6.28318530718;
    //mat2 kernelRot = rotate2D(rotation);

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
    float viewDist = length(fragPos - u_viewPos);
    float distBias = viewDist * 0.0000035;
    float grazingBias = smoothstep(0.45, 0.85, slope) * 0.00022;
    float reconstructBias = 0.0;
    if(lightType == 1){
        float cascadeCount = max(light.shadow.z, 1.0);
        float singleCascadeScale = (cascadeCount <= 1.5) ? 1.20 : 1.0;
        float baseBias = baseOffset * 0.008 * singleCascadeScale;
        float slopeBias = normalOffset * (0.0010 + slope * 0.0085) * singleCascadeScale;
        float minBias = (cascadeCount <= 1.5) ? 0.00036 : 0.00030;
        float maxBias = (cascadeCount <= 1.5) ? 0.00120 : 0.00085;
        reconstructBias = (1.0 - slope) * 0.00022;
        return clamp(baseBias + slopeBias + reconstructBias + distBias + grazingBias, minBias, maxBias);
    }else if(lightType == 2){
        float baseBias = baseOffset * 0.020;
        float slopeBias = normalOffset * (0.0030 + slope * 0.0160);
        reconstructBias = (1.0 - slope) * 0.00020;
        return clamp(baseBias + slopeBias + reconstructBias + distBias + grazingBias, 0.00022, 0.00090);
    }else{
        float baseBias = baseOffset * 0.060;
        float slopeBias = normalOffset * (0.0100 + slope * 0.0400);
        reconstructBias = (1.0 - slope) * 0.00035;
        return clamp(baseBias + slopeBias + reconstructBias + distBias + grazingBias, 0.00045, 0.00190);
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
        float singleCascadeScale = (cascadeCount <= 1.5) ? 1.15 : 1.0;
        float worldOffset = (normalOffset * (0.0026 + grazing * 0.0088) + baseOffset * 0.0011) * singleCascadeScale;
        float worldMax = (cascadeCount <= 1.5) ? 0.00068 : 0.00044;
        worldOffset = clamp(worldOffset, 0.0, worldMax);
        float flatExtra = (1.0 - grazing) * 0.00012;
        float grazingExtra = grazing * 0.00018;
        worldOffset += flatExtra + grazingExtra;
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
    if(geomLen <= 1e-5){
        return baseN;
    }
    vec3 geomN = geom / geomLen;
    if(dot(geomN, baseN) < 0.0){
        geomN = -geomN;
    }
    // Bias/sampling works best with geometric normals, but expose blending to
    // trade seam resistance against contact tightness when tuning shadows.
    float receiverBlend = clamp(u_shadowReceiverNormalBlend, 0.0, 1.0);
    return safeNormalize(mix(baseN, geomN, receiverBlend));
}

float sampleShadowForLight(int lightIndex, Light light, vec3 normal, vec3 fragPos){
    if(light.meta.z < 0.0){
        return 1.0;
    }

    int lightType = int(light.meta.x + 0.5);
    float receiverGradScale = 1.0;
    if(lightType == 1){
        receiverGradScale = (light.shadow.z <= 1.5) ? 0.52 : 0.32;
    }
    int shadowType = resolveEffectiveShadowType(lightIndex, light);
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
    // Pick cascades from the unoffset receiver depth to avoid bias-driven cascade jitter.
    float viewDepth = -(u_cameraView * vec4(fragPos, 1.0)).z;
    if(cascadeCount > 1){
        for(int c = 0; c < 3; ++c){
            if(c >= (cascadeCount - 1)){
                break;
            }
            float split = light.cascadeSplits[c];
            if(viewDepth > split){
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
        float blendFrac = (shadowType == 0) ? 0.12 : 0.30;
        float minBlend = (shadowType == 0) ? 0.75 : 1.50;
        float maxBlendRange = max((shadowType == 0) ? 3.5 : 8.0, splitDist * ((shadowType == 0) ? 0.10 : 0.25));
        float blendRange = clamp(cascadeSpan * blendFrac, minBlend, maxBlendRange);
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

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness){
    vec3 oneMinusRough = vec3(max(1.0 - roughness, 0.0));
    return F0 + (max(oneMinusRough, F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 sampleCubeSpecular(samplerCube cubeMap, vec3 reflectionDir, float roughness){
    float baseSize = float(max(textureSize(cubeMap, 0).x, 1));
    float maxMip = max(log2(baseSize), 0.0);
    float lod = clamp((roughness * roughness) * maxMip, 0.0, maxMip);
    return textureLod(cubeMap, safeNormalize(reflectionDir), lod).rgb;
}

float computeLocalProbeInfluence(vec3 worldPos){
    if(u_useLocalProbe == 0){
        return 0.0;
    }
    vec3 probeCenter = (u_localProbeInfluenceMin + u_localProbeInfluenceMax) * 0.5;
    vec3 probeExtent = max((u_localProbeInfluenceMax - u_localProbeInfluenceMin) * 0.5, vec3(0.001));
    vec3 probeLocal = abs(worldPos - probeCenter) / probeExtent;
    float axis = max(max(probeLocal.x, probeLocal.y), probeLocal.z);
    return 1.0 - smoothstep(0.70, 1.05, axis);
}

vec3 parallaxCorrectLocalProbeDir(vec3 worldPos, vec3 reflectionDir){
    vec3 dir = safeNormalize(reflectionDir);
    vec3 safeDir = dir;
    safeDir.x = (abs(safeDir.x) > 1e-4) ? safeDir.x : ((safeDir.x < 0.0) ? -1e-4 : 1e-4);
    safeDir.y = (abs(safeDir.y) > 1e-4) ? safeDir.y : ((safeDir.y < 0.0) ? -1e-4 : 1e-4);
    safeDir.z = (abs(safeDir.z) > 1e-4) ? safeDir.z : ((safeDir.z < 0.0) ? -1e-4 : 1e-4);

    vec3 tMin = (u_localProbeCaptureMin - worldPos) / safeDir;
    vec3 tMax = (u_localProbeCaptureMax - worldPos) / safeDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    float hitDistance = (tNear > 0.0) ? tNear : tFar;
    if(tFar <= 0.0 || tNear > tFar){
        return dir;
    }

    vec3 hitPos = worldPos + (dir * max(hitDistance, 0.0));
    return safeNormalize(hitPos - u_localProbeCenter);
}

vec3 sampleLocalProbeSpecular(vec3 worldPos, vec3 reflectionDir, float roughness){
    return sampleCubeSpecular(u_localProbe, parallaxCorrectLocalProbeDir(worldPos, reflectionDir), roughness);
}

vec3 sampleEnvironmentSpecular(vec3 reflectionDir, float roughness){
    return sampleCubeSpecular(u_envMap, reflectionDir, roughness);
}

float computeFogFactor(float distanceToCamera){
    if(u_fogEnabled == 0){
        return 0.0;
    }

    float start = max(u_fogStart, 0.0);
    float stop = max(u_fogStop, start);
    float endDistance = max(u_fogEnd, stop);
    if(distanceToCamera <= start){
        return 0.0;
    }

    float nearSpan = max(stop - start, 1e-4);
    float farSpan = max(endDistance - stop, 1e-4);
    float nearPhase = clamp((distanceToCamera - start) / nearSpan, 0.0, 1.0);
    float farPhase = clamp((distanceToCamera - stop) / farSpan, 0.0, 1.0);
    return clamp((nearPhase * 0.5) + (farPhase * 0.5), 0.0, 1.0);
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

int resolveEffectiveShadowType(int lightIndex, Light light){
    int shadowType = int(light.meta.y + 0.5);
    if(shadowType <= 1){
        return shadowType;
    }

    int lightType = int(light.meta.x + 0.5);
    if(lightType == 1){
        return shadowType;
    }

    if(u_debugSelectedLightIndex >= 0 && lightIndex == u_debugSelectedLightIndex){
        return shadowType;
    }

    // Local smooth shadows are the most expensive case; keep full quality only
    // for the selected light and fall back to standard filtering for the rest.
    return 1;
}

void main(){
    vec4 albedoRough = texture(gAlbedo, v_uv);
    vec4 normalMetal = texture(gNormal, v_uv);
    vec4 materialData = texture(gMaterial, v_uv);
    vec4 surfaceData = texture(gSurface, v_uv);
    ivec2 deferredPixel = getDeferredPixelCoord(v_uv);
    float depth = texelFetch(gDepth, deferredPixel, 0).r;

    vec3 albedo = albedoRough.rgb;
    float roughness = clamp(surfaceData.r, 0.04, 1.0);
    float packedMode = normalMetal.a;
    int bsdfModel = 0; // 0=Standard, 1=Glass, 2=Water
    float ao = clamp(surfaceData.g, 0.0, 1.0);
    vec3 emissive = max(materialData.rgb, vec3(0.0));
    float envStrength = max(surfaceData.b, 0.0);
    float transmission = clamp(surfaceData.a, 0.0, 1.0);
    bool hasGeometry = (length(normalMetal.rgb) > 1e-4);

    float ssaoRaw = 1.0;
    float ssaoBlur = 1.0;
    float ssaoFactor = 1.0;
    float combinedAo = ao;
    vec3 giIrradiance = vec3(0.0);

    if(u_lightPassMode == 1 && !hasGeometry){
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    if(u_lightPassMode != 1){
        if(u_useSsao != 0){
            ssaoBlur = clamp(texture(gSsao, v_uv).r, 0.0, 1.0);
            float ssaoOcclusion = clamp(1.0 - ssaoBlur, 0.0, 1.0);
            ssaoFactor = 1.0 - (ssaoOcclusion * max(u_ssaoIntensity, 0.0));
            if(u_ssaoDebugView == 2){
                ssaoRaw = clamp(texture(gSsaoRaw, v_uv).r, 0.0, 1.0);
            }
        }
        ssaoFactor = clamp(ssaoFactor, 0.0, 1.0);
        combinedAo = clamp(ao * ssaoFactor, 0.0, 1.0);
        if(u_useGi != 0){
            giIrradiance = max(texture(gGi, v_uv).rgb, vec3(0.0));
        }
    }

    if(u_ssaoDebugView != 0 && !hasGeometry){
        if(u_ssaoDebugView == 4){
            FragColor = vec4(vec3(0.0), 1.0);
        }else{
            FragColor = vec4(vec3(1.0), 1.0);
        }
        return;
    }
    if(u_ssaoDebugView == 1){
        FragColor = vec4(vec3(1.0 - combinedAo), 1.0);
        return;
    }
    if(u_ssaoDebugView == 2){
        FragColor = vec4(vec3(1.0 - ssaoRaw), 1.0);
        return;
    }
    if(u_ssaoDebugView == 3){
        FragColor = vec4(vec3(1.0 - ao), 1.0);
        return;
    }
    if(u_ssaoDebugView == 4){
        FragColor = vec4(clamp(giIrradiance * 4.0, vec3(0.0), vec3(1.0)), 1.0);
        return;
    }

    vec3 fragPos = reconstructWorldPosition(v_uv, depth);
    vec3 N = safeNormalize(normalMetal.rgb);
    vec3 V = safeNormalize(u_viewPos - fragPos);
    vec3 shadowNormal = computeShadowNormal(N, fragPos);
    int fallbackLightCount = int(u_lightHeader.x + 0.5);
    int tileIndex = getDeferredTileIndex(v_uv);
    int lightCount = getDeferredLightCount(tileIndex, fallbackLightCount);

    bool legacyUnlit = (packedMode < -0.75);
    bool legacyLit = (packedMode < 0.0 && !legacyUnlit);

    float metallic = 0.0;
    if(!legacyLit && !legacyUnlit){
        bsdfModel = clamp(int(floor((packedMode + 1e-4) * 0.5)), 0, 2);
        metallic = clamp(packedMode - (float(bsdfModel) * 2.0), 0.0, 1.0);
    }

    if(legacyUnlit){
        FragColor = (u_lightPassMode == 1) ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(albedo, 1.0);
        return;
    }

    if(legacyLit){
        vec3 LoLegacyDiffuse = vec3(0.0);
        vec3 LoLegacySpec = vec3(0.0);
        for(int listIndex = 0; listIndex < lightCount && listIndex < MAX_LIGHTS; ++listIndex){
            int i = getDeferredLightIndex(tileIndex, listIndex);
            if(i < 0 || i >= MAX_LIGHTS){
                continue;
            }
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

            if(attenuation <= 0.0001 || light.params.x <= 0.0001){
                continue;
            }

            float NdotL = max(dot(N, L), 0.0);
            if(NdotL <= 0.0){
                continue;
            }

            float visibility = 1.0;
            if(light.meta.z >= 0.0){
                visibility = sampleShadowForLight(i, light, shadowNormal, fragPos);
                visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
            }

            vec3 diffuse = NdotL * light.color.rgb * albedo * attenuation * visibility;
            vec3 reflectDir = reflect(-L, N);
            float spec = pow(max(dot(V, reflectDir), 0.0), 32.0);
            vec3 specular = 0.2 * spec * light.color.rgb * attenuation * visibility;

            LoLegacyDiffuse += diffuse * light.params.x;
            LoLegacySpec += specular * light.params.x;
        }

        if(u_lightPassMode == 1){
            FragColor = vec4(max(LoLegacyDiffuse + emissive, vec3(0.0)), 1.0);
            return;
        }

        vec3 indirectLegacy = giIrradiance * albedo;
        vec3 colorLegacy = vec3(0.3) * albedo * combinedAo + LoLegacyDiffuse + LoLegacySpec + emissive + indirectLegacy;
        FragColor = vec4(max(colorLegacy, vec3(0.0)), 1.0);
        return;
    }

    vec3 bsdfAlbedo = albedo;
    if(bsdfModel == 2){
        bsdfAlbedo = mix(albedo, vec3(0.06, 0.32, 0.45), 0.35);
    }

    if(bsdfModel == 1 || bsdfModel == 2){
        metallic = 0.0;
    }

    vec3 F0 = mix(vec3(0.04), bsdfAlbedo, metallic);
    if(bsdfModel == 1){
        F0 = mix(vec3(0.04), bsdfAlbedo, 0.08);
    }else if(bsdfModel == 2){
        F0 = mix(vec3(0.02), bsdfAlbedo, 0.04);
    }

    vec3 LoDiffuse = vec3(0.0);
    vec3 LoSpecular = vec3(0.0);
    vec3 debugColorAccum = vec3(0.0);
    float debugWeight = 0.0;
    for(int listIndex = 0; listIndex < lightCount && listIndex < MAX_LIGHTS; ++listIndex){
        int i = getDeferredLightIndex(tileIndex, listIndex);
        if(i < 0 || i >= MAX_LIGHTS){
            continue;
        }
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

        bool handledShadowDebug = false;
        if(debugModeForLight == 1 && light.meta.z >= 0.0){
            float visibility = sampleShadowForLight(i, light, shadowNormal, fragPos);
            visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
            vec3 debugColor = debugLightColor(i);
            debugColorAccum += mix(debugColor * 0.15, debugColor, visibility);
            debugWeight += 1.0;
            handledShadowDebug = true;
        }else if((debugModeForLight == 2 || debugModeForLight == 3) &&
                 light.meta.z >= 0.0 && lightType != 0){
            vec3 shadowPosDebug = offsetShadowReceiver(light, shadowNormal, fragPos);
            int cascadeCountDebug = clamp(int(light.shadow.z + 0.5), 1, 4);
            int cascadeIndexDebug = 0;
            float viewDepthDebug = -(u_cameraView * vec4(fragPos, 1.0)).z;
            if(cascadeCountDebug > 1){
                for(int c = 0; c < 3; ++c){
                    if(c >= (cascadeCountDebug - 1)){
                        break;
                    }
                    float splitD = light.cascadeSplits[c];
                    if(viewDepthDebug > splitD){
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
            handledShadowDebug = true;
        }

        if(handledShadowDebug){
            continue;
        }

        if(attenuation <= 0.0001 || light.params.x <= 0.0001){
            continue;
        }

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL <= 0.0){
            continue;
        }

        float visibility = 1.0;
        if(light.meta.z >= 0.0){
            visibility = sampleShadowForLight(i, light, shadowNormal, fragPos);
            visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
        }

        vec3 H = safeNormalize(V + L);

        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
        vec3 specular = numerator / denom;

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        if(bsdfModel == 1){
            // Keep a small diffuse/transmission lobe so glass does not collapse to near-black.
            kD = vec3(0.05 * (1.0 - roughness));
            specular *= 1.65;
        }else if(bsdfModel == 2){
            kD *= 0.35;
            specular *= 1.18;
        }

        vec3 radiance = light.color.rgb * light.params.x * attenuation * visibility;
        vec3 diffuseTerm = (kD * bsdfAlbedo / PI) * radiance * NdotL;
        vec3 specularTerm = specular * radiance * NdotL;
        if(bsdfModel == 1 || bsdfModel == 2){
            float glintPower = mix(180.0, 1400.0, 1.0 - roughness);
            float glint = pow(max(dot(reflect(-L, N), V), 0.0), glintPower);
            float glintStrength = ((bsdfModel == 1) ? 2.2 : 1.4) * (0.35 + transmission);
            specularTerm += radiance * glint * glintStrength;
        }

        LoDiffuse += diffuseTerm;
        LoSpecular += specularTerm;
    }

    if(u_lightPassMode == 1){
        FragColor = vec4(max(LoDiffuse + emissive, vec3(0.0)), 1.0);
        return;
    }

    vec3 ambient = u_ambientColor.rgb * max(u_ambientIntensity, 0.0) * bsdfAlbedo * combinedAo;
    if(bsdfModel == 1){
        ambient *= 0.22;
    }else if(bsdfModel == 2){
        ambient *= 0.45;
    }
    vec3 envSpec = vec3(0.0);
    if(u_useEnvMap != 0 || u_useLocalProbe != 0){
        vec3 R = reflect(-V, N);
        float NdotV = max(dot(N, V), 0.0);
        vec3 Fenv = FresnelSchlickRoughness(NdotV, F0, roughness);
        float localProbeInfluence = computeLocalProbeInfluence(fragPos);
        vec3 envSample = vec3(0.0);
        if(u_useEnvMap != 0){
            envSample = sampleEnvironmentSpecular(R, roughness);
        }
        if(localProbeInfluence > 1e-4){
            vec3 localProbeSample = sampleLocalProbeSpecular(fragPos, R, roughness);
            envSample = (u_useEnvMap != 0)
                ? mix(envSample, localProbeSample, localProbeInfluence)
                : localProbeSample;
        }
        float envContrib = envStrength * (1.0 - roughness) * combinedAo;
        float viewDistance = length(fragPos - u_viewPos);
        if(bsdfModel == 1){
            envContrib = envStrength * mix(0.55, 1.60, 1.0 - roughness) * (0.45 + (0.55 * combinedAo));
        }else if(bsdfModel == 2){
            envContrib = envStrength * mix(0.25, 0.95, 1.0 - roughness) * (0.45 + (0.55 * combinedAo));
        }else{
            float smoothness = 1.0 - roughness;
            envContrib *= mix(0.30, 1.35, metallic) * mix(0.20, 1.0, smoothness * smoothness);
        }
        // Reduce sky reflection on flat or distant rough opaque surfaces.
        if(bsdfModel == 0){
            float flatFade = 1.0 - smoothstep(0.55, 0.94, N.y);
            flatFade = mix(flatFade, 1.0, metallic);
            float roughFarFade =
                1.0 -
                (smoothstep(0.22, 0.70, roughness) *
                 (1.0 - metallic) *
                 smoothstep(24.0, 120.0, viewDistance));
            envContrib *= flatFade * roughFarFade;
        }
        envSpec = envSample * Fenv * envContrib;
    }

    vec3 indirectDiffuse = giIrradiance * bsdfAlbedo * (1.0 - metallic);
    if(bsdfModel == 1){
        indirectDiffuse *= 0.15;
    }else if(bsdfModel == 2){
        indirectDiffuse *= 0.50;
    }
    vec3 color = ambient + LoDiffuse + LoSpecular + envSpec + emissive + indirectDiffuse;
    if(bsdfModel == 1){
        vec3 transmissionTint = mix(vec3(1.0), bsdfAlbedo, 0.20);
        color += (LoDiffuse + ambient + indirectDiffuse) * transmission * transmissionTint * 0.65;
        color += transmissionTint * (0.035 + (0.09 * transmission));
    }else if(bsdfModel == 2){
        vec3 waterScatter = vec3(0.02, 0.08, 0.12) * (0.35 + (0.65 * (1.0 - roughness)));
        color += waterScatter * (0.35 + transmission);
    }
    float fogFactor = computeFogFactor(length(fragPos - u_viewPos));
    color = mix(color, u_fogColor.rgb, fogFactor);
    if(debugWeight > 0.0){
        FragColor = vec4(clamp(debugColorAccum / debugWeight, 0.0, 1.0), 1.0);
        return;
    }
    FragColor = vec4(max(color, vec3(0.0)), ao);
}
