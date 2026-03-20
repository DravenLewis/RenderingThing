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

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;
in vec4 v_tangent;

out vec4 FragColor;

uniform vec4 u_baseColor;
uniform sampler2D u_baseColorTex;
uniform int u_useBaseColorTex;

uniform float u_metallic;
uniform float u_roughness;
uniform sampler2D u_roughnessTex;
uniform int u_useRoughnessTex;
uniform sampler2D u_metallicRoughnessTex;
uniform int u_useMetallicRoughnessTex;

uniform sampler2D u_normalTex;
uniform int u_useNormalTex;
uniform float u_normalScale;
uniform sampler2D u_heightTex;
uniform int u_useHeightTex;
uniform float u_heightScale;

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
uniform samplerCube u_localProbe;
uniform int u_useLocalProbe;
uniform vec3 u_localProbeCenter;
uniform vec3 u_localProbeCaptureMin;
uniform vec3 u_localProbeCaptureMax;
uniform vec3 u_localProbeInfluenceMin;
uniform vec3 u_localProbeInfluenceMax;

uniform vec2 u_uvScale;
uniform vec2 u_uvOffset;

uniform vec3 u_viewPos;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform int u_receiveShadows;
uniform int u_debugShadows; // 0=off,1=visibility,2=cascade index,3=proj bounds
uniform int u_debugSelectedLightIndex;
uniform int u_useAlphaClip;
uniform float u_alphaCutoff;
uniform float u_transmission;
uniform float u_ior;
uniform float u_thickness;
uniform vec3 u_attenuationColor;
uniform float u_attenuationDistance;
uniform float u_scatteringStrength;
uniform int u_bsdfModel; // 0=Standard, 1=Glass, 2=Water
uniform float u_time;
uniform int u_enableWaveDisplacement;
uniform float u_waveAmplitude;
uniform float u_waveFrequency;
uniform float u_waveSpeed;
uniform float u_waveChoppiness;
uniform float u_waveSecondaryScale;
uniform vec2 u_waveDirection;
uniform float u_waveTextureInfluence;
uniform vec2 u_waveTextureSpeed;
uniform sampler2D u_ssrColor;
uniform int u_useSceneColor;
uniform sampler2D u_sceneDepth;
uniform int u_useSceneDepth;
uniform int u_useSsr;
uniform float u_ssrIntensity;
uniform float u_ssrMaxDistance;
uniform float u_ssrThickness;
uniform float u_ssrStride;
uniform float u_ssrJitter;
uniform int u_ssrMaxSteps;
uniform float u_ssrRoughnessCutoff;
uniform float u_ssrEdgeFade;
uniform mat4 u_invProjection;
uniform sampler2D u_planarReflectionTex;
uniform int u_usePlanarReflection;
uniform mat4 u_planarReflectionMatrix;
uniform float u_planarReflectionStrength;
uniform vec3 u_planarReflectionCenter;
uniform vec3 u_planarReflectionNormal;
uniform float u_planarReflectionReceiverFadeDistance;

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;      // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

uniform sampler2D u_shadowMaps2D[MAX_SHADOW_MAPS_2D];
uniform samplerCubeShadow u_shadowMapsCube[MAX_SHADOW_MAPS_CUBE];

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

float hash12(vec2 p){
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 safeNormalize2(vec2 v){
    float lenV = length(v);
    return (lenV > 1e-5) ? (v / lenV) : vec2(0.8, 0.6);
}

float dielectricF0(float ior){
    float clampedIor = clamp(ior, 1.0, 2.5);
    float r = (clampedIor - 1.0) / max(clampedIor + 1.0, 1e-4);
    return clamp(r * r, 0.0, 1.0);
}

float sampleProceduralWave(vec2 wavePos, float timeSec){
    vec2 dir = safeNormalize2(u_waveDirection);
    vec2 orthoDir = vec2(-dir.y, dir.x);
    float safeFreq = max(u_waveFrequency, 0.0001);
    float secondaryScale = max(u_waveSecondaryScale, 0.05);
    float safeSpeed = u_waveSpeed;

    float phase0 = dot(wavePos, dir) * safeFreq + (timeSec * safeSpeed);
    float phase1 = dot(wavePos, orthoDir) * safeFreq * (1.37 * secondaryScale) - (timeSec * safeSpeed * 1.21) + 0.70;
    float phase2 = (wavePos.x + wavePos.y) * safeFreq * (2.11 * secondaryScale) + (timeSec * safeSpeed * 0.83);

    float primary = sin(phase0);
    float secondary = sin(phase1) * 0.55;
    float tertiary = cos(phase2) * 0.30;
    float choppy = sin((phase0 * 1.91) + (timeSec * safeSpeed * 1.47)) * (0.45 * clamp(u_waveChoppiness, 0.0, 1.0));
    return (primary + secondary + tertiary + choppy) * 0.5;
}

float sampleTextureWave(vec2 uv, float timeSec){
    if(u_useHeightTex == 0 || u_waveTextureInfluence <= 1e-4){
        return 0.0;
    }
    vec2 flowUv = uv + (u_waveTextureSpeed * timeSec);
    float texWave = texture(u_heightTex, flowUv).r * 2.0 - 1.0;
    return texWave * clamp(u_waveTextureInfluence, 0.0, 4.0);
}

float sampleWaterWaveField(vec2 wavePos, vec2 uv, float timeSec){
    return sampleProceduralWave(wavePos, timeSec) + sampleTextureWave(uv, timeSec);
}

vec3 computeAttenuationCoefficient(vec3 attenuationColor, float attenuationDistance){
    vec3 safeColor = clamp(attenuationColor, vec3(0.001), vec3(0.9999));
    return -log(safeColor) / max(attenuationDistance, 0.001);
}

vec3 computeVolumeTransmittance(vec3 attenuationColor, float attenuationDistance, float distanceThroughMedium){
    vec3 coeff = computeAttenuationCoefficient(attenuationColor, attenuationDistance);
    return exp(-coeff * max(distanceThroughMedium, 0.0));
}

vec3 reconstructSceneViewPosition(vec2 uv){
    float depth = texture(u_sceneDepth, uv).r;
    vec4 clip = vec4((uv * 2.0) - 1.0, (depth * 2.0) - 1.0, 1.0);
    vec4 view = u_invProjection * clip;
    if(abs(view.w) <= 1e-5){
        return vec3(0.0, 0.0, -1.0);
    }
    return view.xyz / view.w;
}

vec2 clampUv(vec2 uv);

vec2 projectViewPositionToUv(vec3 viewPos){
    vec4 clip = u_projection * vec4(viewPos, 1.0);
    if(abs(clip.w) <= 1e-5){
        return vec2(-1.0);
    }
    return (clip.xy / clip.w) * 0.5 + 0.5;
}

bool isUvInBounds(vec2 uv){
    return uv.x > 0.001 && uv.x < 0.999 && uv.y > 0.001 && uv.y < 0.999;
}

bool traceSceneDepthRay(vec3 originView, vec3 dirView, float jitter, out vec2 outHitUv, out float outHitDistance){
    float stride = max(u_ssrStride, 0.25);
    float thicknessBase = max(u_ssrThickness, 0.005);
    float traveled = 0.0;
    vec4 startClip = u_projection * vec4(originView, 1.0);
    if(abs(startClip.w) <= 1e-5){
        return false;
    }

    vec2 startUv = (startClip.xy / startClip.w) * 0.5 + 0.5;
    vec2 depthTexel = 1.0 / max(vec2(textureSize(u_sceneDepth, 0)), vec2(1.0));
    vec2 minScreenStep = depthTexel * 1.0;
    float minTravel = max(stride * 0.35, thicknessBase * 1.25);
    float prevGap = -1e9;
    vec3 prevRayPos = originView;
    bool hasPrev = false;
    vec3 rayPos = originView + (dirView * max(stride * mix(0.10, 0.65, jitter), 0.015));
    int stepCount = clamp(u_ssrMaxSteps, 8, 256);
    for(int i = 0; i < stepCount; ++i){
        traveled = length(rayPos - originView);
        if(traveled > u_ssrMaxDistance){
            break;
        }

        vec4 clip = u_projection * vec4(rayPos, 1.0);
        if(abs(clip.w) <= 1e-5){
            break;
        }

        vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
        if(uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0){
            break;
        }

        if((abs(uv.x - startUv.x) <= minScreenStep.x) &&
           (abs(uv.y - startUv.y) <= minScreenStep.y) &&
           traveled < minTravel){
            rayPos += dirView * stride;
            continue;
        }

        float sceneDepth = texture(u_sceneDepth, uv).r;
        if(sceneDepth >= 0.999999){
            hasPrev = false;
            rayPos += dirView * stride;
            continue;
        }

        vec3 scenePosView = reconstructSceneViewPosition(uv);
        float gap = scenePosView.z - rayPos.z;
        float pixelThickness = max(depthTexel.x, depthTexel.y) * max(abs(rayPos.z), 1.0) * 1.5;
        float thickness = (thicknessBase * mix(1.0, 2.6, clamp(traveled / max(u_ssrMaxDistance, 0.001), 0.0, 1.0))) + pixelThickness;
        if(abs(gap) <= thickness && traveled >= minTravel){
            outHitUv = clampUv(uv);
            outHitDistance = traveled;
            return true;
        }
        if(hasPrev && prevGap < 0.0 && gap >= -thickness){
            vec3 refineA = prevRayPos;
            vec3 refineB = rayPos;
            vec2 refinedUv = uv;
            float refinedGap = gap;
            for(int refine = 0; refine < 5; ++refine){
                vec3 mid = (refineA + refineB) * 0.5;
                vec4 midClip = u_projection * vec4(mid, 1.0);
                if(abs(midClip.w) <= 1e-5){
                    break;
                }

                vec2 midUv = (midClip.xy / midClip.w) * 0.5 + 0.5;
                if(midUv.x <= 0.0 || midUv.x >= 1.0 || midUv.y <= 0.0 || midUv.y >= 1.0){
                    break;
                }

                float midDepth = texture(u_sceneDepth, midUv).r;
                if(midDepth >= 0.999999){
                    refineA = mid;
                    continue;
                }

                vec3 midScenePos = reconstructSceneViewPosition(midUv);
                float midGap = midScenePos.z - mid.z;
                if(midGap < 0.0){
                    refineA = mid;
                }else{
                    refineB = mid;
                    refinedUv = midUv;
                    refinedGap = midGap;
                }
            }

            if(abs(refinedGap) <= thickness * 1.5){
                outHitUv = clampUv(refinedUv);
                outHitDistance = length(((refineA + refineB) * 0.5) - originView);
                return true;
            }
        }

        hasPrev = true;
        prevGap = gap;
        prevRayPos = rayPos;

        float adaptiveStep = stride * mix(
            0.55,
            1.30,
            clamp(abs(gap) / max(abs(scenePosView.z), 0.5), 0.0, 1.0)
        );
        rayPos += dirView * adaptiveStep;
    }
    return false;
}

mat2 rotate2D(float angle){
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c);
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
    // Avoid floor(receiverPos*32): small camera moves would snap rotation and cause banding.
    float rotation = hash13(receiverPos * 0.754877666 + vec3(float(mapIndex), float(shadowType), 0.0)) * 6.28318530718;
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

mat3 buildTBN(vec2 uv, vec3 N, vec4 tangentData){
    vec3 tangent = tangentData.xyz;
    float tangentLen = length(tangent);
    if(tangentLen > 1e-5){
        vec3 T = tangent / tangentLen;
        T = safeNormalize(T - N * dot(N, T));
        float handedness = (tangentData.w < 0.0) ? -1.0 : 1.0;
        vec3 B = safeNormalize(cross(N, T)) * handedness;
        return mat3(T, B, N);
    }

    vec3 dp1 = dFdx(v_fragPos);
    vec3 dp2 = dFdy(v_fragPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    float det = (duv1.x * duv2.y) - (duv1.y * duv2.x);
    if(abs(det) < 1e-10){
        // UV derivatives can become degenerate at steep viewing angles; build a stable basis from N.
        vec3 up = (abs(N.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 T = safeNormalize(cross(up, N));
        vec3 B = safeNormalize(cross(N, T));
        return mat3(T, B, N);
    }

    float invDet = 1.0 / det;
    vec3 T = (dp1 * duv2.y - dp2 * duv1.y) * invDet;
    vec3 B = (dp2 * duv1.x - dp1 * duv2.x) * invDet;

    T = safeNormalize(T - N * dot(N, T));
    B = safeNormalize(B - N * dot(N, B));

    if(dot(cross(T, B), N) < 0.0){
        T = -T;
    }

    B = safeNormalize(cross(N, T));
    return mat3(T, B, N);
}

vec2 applyHeightMapParallax(vec2 uv, vec2 duvDx, vec2 duvDy, vec3 viewDirWS, mat3 TBN){
    // Height/parallax mapping in this derivative-based path can introduce visible seams
    // across broad surfaces; keep UVs stable until tangent-space parallax is reworked.
    return uv;
}

vec3 getNormal(vec2 uv, vec2 duvDx, vec2 duvDy, vec3 N, mat3 TBN, vec3 viewDirWS){
    if(u_useNormalTex == 0){
        return N;
    }

    vec3 mapN = textureGrad(u_normalTex, uv, duvDx, duvDy).xyz * 2.0 - 1.0;
    mapN.xy *= u_normalScale;
    mapN = safeNormalize(mapN);
    return safeNormalize(TBN * mapN);
}

vec3 applyWaterSurfaceNormal(vec3 baseNormal, mat3 TBN, vec2 uv, vec3 worldPos, out float outSlope){
    outSlope = 0.0;
    if(u_enableWaveDisplacement == 0 || u_waveAmplitude <= 1e-5){
        return baseNormal;
    }

    float eps = 0.07;
    float center = sampleWaterWaveField(worldPos.xz, uv, u_time);
    float sampleX = sampleWaterWaveField(worldPos.xz + vec2(eps, 0.0), uv + vec2(eps * 0.12, 0.0), u_time);
    float sampleZ = sampleWaterWaveField(worldPos.xz + vec2(0.0, eps), uv + vec2(0.0, eps * 0.12), u_time);
    float slopeScale = (max(u_waveAmplitude, 0.01) * 18.0) * mix(0.85, 1.35, clamp(u_normalScale * 0.5, 0.0, 1.0));
    vec3 waveTs = safeNormalize(vec3((center - sampleX) * slopeScale, (center - sampleZ) * slopeScale, 1.0));
    outSlope = clamp(length(waveTs.xy), 0.0, 1.0);
    vec3 waveWs = safeNormalize(TBN * waveTs);
    float blend = clamp(0.35 + (u_waveChoppiness * 0.45), 0.0, 0.90);
    return safeNormalize(mix(baseNormal, waveWs, blend));
}

float applyOcclusionStrength(float occlusionSample, float strength){
    float occlusion = clamp(occlusionSample, 0.0, 1.0);
    float effectStrength = max(strength, 0.0);
    return clamp(1.0 - ((1.0 - occlusion) * effectStrength), 0.0, 1.0);
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

float applySpecularAaRoughness(float roughness, vec3 normal){
    vec3 dndx = dFdx(normal);
    vec3 dndy = dFdy(normal);
    float variance = min(dot(dndx, dndx) + dot(dndy, dndy), 0.40);
    float kernelRoughness2 = min(variance * 0.50, 0.18);
    float roughness2 = clamp((roughness * roughness) + kernelRoughness2, 0.0016, 1.0);
    return clamp(sqrt(roughness2), 0.04, 1.0);
}

vec3 sampleEnvironmentSpecular(vec3 reflectionDir, float roughness){
    float baseSize = float(max(textureSize(u_envMap, 0).x, 1));
    float maxMip = max(log2(baseSize), 0.0);
    float lod = clamp((roughness * roughness) * maxMip, 0.0, maxMip);
    return textureLod(u_envMap, safeNormalize(reflectionDir), lod).rgb;
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
    float baseSize = float(max(textureSize(u_localProbe, 0).x, 1));
    float maxMip = max(log2(baseSize), 0.0);
    float lod = clamp((roughness * roughness) * maxMip, 0.0, maxMip);
    return textureLod(u_localProbe, parallaxCorrectLocalProbeDir(worldPos, reflectionDir), lod).rgb;
}

vec3 samplePlanarReflection(vec3 worldPos, vec3 worldReflectDir, float roughness, out float weight){
    weight = 0.0;
    if(u_usePlanarReflection == 0){
        return vec3(0.0);
    }

    vec3 planeNormal = safeNormalize(u_planarReflectionNormal);
    float fadeDistance = max(u_planarReflectionReceiverFadeDistance, 0.05);
    float planeDistance = dot(worldPos - u_planarReflectionCenter, planeNormal);
    float receiverFade = 1.0 - smoothstep(fadeDistance * 0.60, fadeDistance, abs(planeDistance));
    if(receiverFade <= 1e-4){
        return vec3(0.0);
    }

    vec3 reflectDir = safeNormalize(worldReflectDir);
    float planeDenom = dot(reflectDir, planeNormal);
    float planeAngleFade = smoothstep(0.02, 0.12, abs(planeDenom));
    if(planeAngleFade <= 1e-4){
        return vec3(0.0);
    }

    vec2 texSize = vec2(textureSize(u_planarReflectionTex, 0));
    if(texSize.x <= 1.0 || texSize.y <= 1.0){
        return vec3(0.0);
    }

    float hitEpsilon = mix(0.015, 0.08, clamp(roughness, 0.0, 1.0));
    float hitT = -planeDistance / planeDenom;
    if(hitT < -hitEpsilon){
        return vec3(0.0);
    }

    vec3 hitPos = worldPos + (reflectDir * max(hitT, 0.0));
    vec4 reflectedClip = u_planarReflectionMatrix * vec4(hitPos, 1.0);
    if(reflectedClip.w <= 1e-4){
        return vec3(0.0);
    }

    vec2 uv = (reflectedClip.xy / reflectedClip.w) * 0.5 + 0.5;
    if(any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))){
        return vec3(0.0);
    }

    float maxMip = max(log2(max(texSize.x, texSize.y)), 0.0);
    float lod = clamp((roughness * roughness) * maxMip, 0.0, maxMip);
    float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    weight = smoothstep(0.0, 0.05, edge) * receiverFade * planeAngleFade;
    return textureLod(u_planarReflectionTex, clamp(uv, vec2(0.001), vec2(0.999)), lod).rgb;
}

vec2 clampUv(vec2 uv){
    return clamp(uv, vec2(0.001), vec2(0.999));
}

vec3 sampleSceneColorRough(sampler2D sceneTexture, vec2 uv, float roughness, vec2 texelSize){
    uv = clampUv(uv);
    float blur = clamp(roughness * roughness, 0.0, 1.0);
    if(blur <= 1e-4){
        return texture(sceneTexture, uv).rgb;
    }

    float radius = mix(0.65, 6.0, blur);
    vec2 axisX = vec2(texelSize.x * radius, 0.0);
    vec2 axisY = vec2(0.0, texelSize.y * radius);
    vec2 diag = texelSize * radius * 0.70710678;

    vec3 color = texture(sceneTexture, uv).rgb * 0.28;
    color += texture(sceneTexture, clampUv(uv + axisX)).rgb * 0.11;
    color += texture(sceneTexture, clampUv(uv - axisX)).rgb * 0.11;
    color += texture(sceneTexture, clampUv(uv + axisY)).rgb * 0.11;
    color += texture(sceneTexture, clampUv(uv - axisY)).rgb * 0.11;
    color += texture(sceneTexture, clampUv(uv + diag)).rgb * 0.07;
    color += texture(sceneTexture, clampUv(uv + vec2(-diag.x, diag.y))).rgb * 0.07;
    color += texture(sceneTexture, clampUv(uv + vec2(diag.x, -diag.y))).rgb * 0.07;
    color += texture(sceneTexture, clampUv(uv - diag)).rgb * 0.07;
    return color;
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

void main() {
    vec2 uvBase = v_uv * u_uvScale + u_uvOffset;
    vec2 duvDx = dFdx(uvBase);
    vec2 duvDy = dFdy(uvBase);
    vec3 V = safeNormalize(u_viewPos - v_fragPos);
    vec3 baseN = safeNormalize(v_normal);
    mat3 TBN = buildTBN(uvBase, baseN, v_tangent);
    vec2 uv = applyHeightMapParallax(uvBase, duvDx, duvDy, V, TBN);
    int bsdfModel = clamp(u_bsdfModel, 0, 2);
    vec4 baseTex = (u_useBaseColorTex != 0) ? textureGrad(u_baseColorTex, uv, duvDx, duvDy) : vec4(1.0);
    float alphaTex = (bsdfModel == 0) ? baseTex.a : 1.0;
    vec4 baseColor = vec4(
        (u_baseColor.rgb * baseTex.rgb) * v_color.rgb,
        u_baseColor.a * alphaTex * v_color.a
    );
    float transmission = 0.0;
    if(bsdfModel != 0){
        float legacyTransmission = clamp(1.0 - baseColor.a, 0.0, 1.0);
        transmission = clamp((u_transmission > 1e-4) ? u_transmission : legacyTransmission, 0.0, 1.0);
    }

    if(bsdfModel == 0 && u_useAlphaClip != 0 && baseColor.a < u_alphaCutoff){
        discard;
    }

    float metallic = u_metallic;
    float roughness = clamp(u_roughness, 0.04, 1.0);
    if(u_useRoughnessTex != 0){
        float roughSample = textureGrad(u_roughnessTex, uv, duvDx, duvDy).r;
        roughness = clamp(roughness * roughSample, 0.04, 1.0);
    }
    if(u_useMetallicRoughnessTex != 0){
        vec4 mr = textureGrad(u_metallicRoughnessTex, uv, duvDx, duvDy);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
    }

    float ao = 1.0;
    if(u_useOcclusionTex != 0){
        float occl = textureGrad(u_occlusionTex, uv, duvDx, duvDy).r;
        ao = applyOcclusionStrength(occl, u_aoStrength);
    }

    vec3 N = getNormal(uv, duvDx, duvDy, baseN, TBN, V);
    float waterWaveSlope = 0.0;
    if(bsdfModel == 2){
        N = applyWaterSurfaceNormal(N, TBN, uv, v_fragPos, waterWaveSlope);
        roughness = clamp(max(roughness, waterWaveSlope * 0.16), 0.04, 1.0);
    }
    vec3 specularAaNormal = baseN;
    if(bsdfModel == 1 || bsdfModel == 2){
        N = faceforward(N, -V, N);
        specularAaNormal = faceforward(baseN, -V, baseN);
    }
    // Use geometric normal for specular AA; mapped-normal derivatives can create
    // visible screen-space seams on broad close-up surfaces.
    roughness = applySpecularAaRoughness(roughness, specularAaNormal);

    vec3 albedo = baseColor.rgb;
    vec3 bsdfAlbedo = albedo;
    if(bsdfModel == 2){
        bsdfAlbedo = mix(albedo, vec3(0.06, 0.32, 0.45), 0.35);
    }

    if(bsdfModel == 1 || bsdfModel == 2){
        metallic = 0.0;
    }

    float materialIor = clamp(u_ior, 1.0, 2.5);
    float dielectricReflectance = dielectricF0(materialIor);
    vec3 F0 = (bsdfModel == 0)
        ? mix(vec3(0.04), bsdfAlbedo, metallic)
        : vec3(dielectricReflectance);

    vec3 Lo = vec3(0.0);
    vec3 debugColorAccum = vec3(0.0);
    float debugWeight = 0.0;
    int lightCount = int(u_lightHeader.x + 0.5);
    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        Light light = u_lights[i];
        int lightType = int(light.meta.x + 0.5);
        int debugModeForLight = resolveShadowDebugMode(i, light);

        vec3 L = vec3(0.0);
        float attenuation = 1.0;
        float spotFactor = 1.0;

        if (lightType == 0) {
            vec3 toLight = light.position.xyz - v_fragPos;
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
        } else if (lightType == 1) {
            L = safeNormalize(-light.direction.xyz);
        } else if (lightType == 2) {
            vec3 toLight = light.position.xyz - v_fragPos;
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
        if(bsdfModel == 1){
            kD = vec3(0.0);
            specular *= 1.08;
        }else if(bsdfModel == 2){
            kD *= 0.06 * clamp(u_scatteringStrength, 0.0, 4.0);
            specular *= 1.04;
        }

        vec3 specularLighting = specular;
        if(bsdfModel == 2 && waterWaveSlope > 1e-4){
            float glintPower = mix(220.0, 900.0, 1.0 - roughness);
            float glint = pow(max(dot(reflect(-L, N), V), 0.0), glintPower);
            float glintStrength = waterWaveSlope * clamp(u_scatteringStrength, 0.0, 2.0) * 0.14;
            specularLighting += vec3(glint * glintStrength);
        }

        vec3 radiance = light.color.rgb * light.params.x * attenuation * spotFactor;
        vec3 lightContribution = (kD * bsdfAlbedo / PI + specularLighting) * radiance * NdotL;

        if(u_receiveShadows != 0 && light.meta.z >= 0.0){
            vec3 shadowNormal = baseN;
            float bias = computeShadowBias(light, shadowNormal, v_fragPos);
            vec3 shadowPos = offsetShadowReceiver(light, shadowNormal, v_fragPos);
            float visibility = 1.0;
            int lType = int(light.meta.x + 0.5);
            float receiverGradScale = 1.0;
            if(lType == 1){
                receiverGradScale = (light.shadow.z <= 1.5) ? 0.42 : 0.24;
            }
            int sType = int(light.meta.y + 0.5);
            int baseIndex = int(light.meta.z + 0.5);
            float filterScale = 1.0;
            if(lType == 1 && sType != 0){
                float receiverNdotL = max(dot(shadowNormal, getShadowDirection(light, shadowPos)), 0.0);
                float grazing = 1.0 - receiverNdotL;
                float grazingFilterMin = (sType == 2) ? 0.55 : 0.70;
                filterScale = mix(1.0, grazingFilterMin, grazing);
            }
            int cascadeCount = 1;
            int cascadeIndex = 0;
            bool hasProjectionBounds = false;
            bool projectionOutOfBounds = false;
            float projectionDepth = 0.0;

            if(lType == 0){
                visibility = sampleShadowCube(sType, baseIndex, shadowPos, light.position.xyz, max(light.cascadeSplits.x, 0.1), bias);
            }else{
                cascadeCount = clamp(int(light.shadow.z + 0.5), 1, 4);
                float viewDepth = -(u_view * vec4(shadowPos, 1.0)).z;
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
                vec4 lightSpacePos = light.lightMatrices[cascadeIndex] * vec4(shadowPos, 1.0);
                if(debugModeForLight == 3){
                    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
                    projCoords = projCoords * 0.5 + 0.5;
                    hasProjectionBounds = true;
                    projectionOutOfBounds = (projCoords.x < 0.0 || projCoords.x > 1.0 ||
                                             projCoords.y < 0.0 || projCoords.y > 1.0 ||
                                             projCoords.z < 0.0 || projCoords.z > 1.0);
                    projectionDepth = clamp(projCoords.z, 0.0, 1.0);
                }
                visibility = sampleShadow2D(sType, baseIndex + cascadeIndex, lightSpacePos, bias, filterScale, shadowPos, receiverGradScale);
                if(lType == 1 && cascadeCount > 1 && cascadeIndex == (cascadeCount - 1) && sType != 0){
                    float prevSplit = (cascadeIndex > 0) ? light.cascadeSplits[cascadeIndex - 1] : 0.0;
                    float splitDist = light.cascadeSplits[cascadeIndex];
                    float cascadeSpan = max(splitDist - prevSplit, 0.001);
                    float localCascadeT = clamp((viewDepth - prevSplit) / cascadeSpan, 0.0, 1.0);
                    float hardVisibility = sampleShadow2D(0, baseIndex + cascadeIndex, lightSpacePos, bias, 1.0, shadowPos, 0.0);
                    float filterToHard = smoothstep(0.25, 0.95, localCascadeT);
                    visibility = mix(visibility, hardVisibility, filterToHard);
                }
                if(lType == 1 && cascadeCount > 1 && cascadeIndex < (cascadeCount - 1)){
                    float splitDist = light.cascadeSplits[cascadeIndex];
                    float prevSplit = (cascadeIndex > 0) ? light.cascadeSplits[cascadeIndex - 1] : 0.0;
                    float cascadeSpan = max(splitDist - prevSplit, 0.001);
                    float maxBlendRange = max(16.0, splitDist * 0.40);
                    float blendRange = clamp(cascadeSpan * 0.55, 1.5, maxBlendRange);
                    float blendStart = splitDist - blendRange;
                    float blendT = clamp((viewDepth - blendStart) / blendRange, 0.0, 1.0);
                    if(blendT > 0.0){
                        vec4 nextLightSpacePos = light.lightMatrices[cascadeIndex + 1] * vec4(shadowPos, 1.0);
                        float nextVisibility = sampleShadow2D(sType, baseIndex + cascadeIndex + 1, nextLightSpacePos, bias, filterScale, shadowPos, receiverGradScale);
                        visibility = mix(visibility, nextVisibility, blendT);
                    }
                }
                if(lType == 1){
                    float maxShadowDepth = light.cascadeSplits[cascadeCount - 1];
                    if(maxShadowDepth > 0.0){
                        float fadeStart = maxShadowDepth * ((sType == 0) ? 0.985 : 0.94);
                        float fadeWidth = max(maxShadowDepth - fadeStart, (sType == 0) ? 2.5 : 6.0);
                        float farFade = clamp((viewDepth - fadeStart) / fadeWidth, 0.0, 1.0);
                        visibility = mix(visibility, 1.0, farFade);
                    }
                }
            }

            visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
            lightContribution *= visibility;
            if(debugModeForLight == 1){
                vec3 debugColor = debugLightColor(i);
                debugColorAccum += mix(debugColor * 0.15, debugColor, visibility);
                debugWeight += 1.0;
            }else if(debugModeForLight == 2 && lType != 0){
                vec3 debugColor = mix(debugLightColor(i), debugCascadeColor(cascadeIndex), 0.55);
                debugColorAccum += debugColor;
                debugWeight += 1.0;
            }else if(debugModeForLight == 3 && lType != 0 && hasProjectionBounds){
                vec3 debugColor = debugLightColor(i);
                if(projectionOutOfBounds){
                    debugColor = mix(debugColor, vec3(1.0, 0.12, 0.08), 0.75);
                }else{
                    debugColor *= (0.4 + (0.6 * projectionDepth));
                }
                debugColorAccum += debugColor;
                debugWeight += 1.0;
            }
        }

        Lo += lightContribution;
    }

    vec3 ambient = vec3(0.03) * bsdfAlbedo * ao;
    if(bsdfModel == 1){
        ambient *= 0.08;
    }else if(bsdfModel == 2){
        ambient *= mix(0.10, 0.22, clamp(u_scatteringStrength * 0.35, 0.0, 1.0));
    }
    vec3 envSpec = vec3(0.0);
    vec3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);
    vec3 Fenv = FresnelSchlickRoughness(NdotV, F0, roughness);
    float localProbeInfluence = computeLocalProbeInfluence(v_fragPos);
    float envContrib = u_envStrength * (1.0 - roughness) * ao;
    if(bsdfModel == 1){
        envContrib = u_envStrength * mix(0.55, 1.60, 1.0 - roughness) * (0.45 + (0.55 * ao));
    }else if(bsdfModel == 2){
        envContrib = u_envStrength * mix(0.25, 0.95, 1.0 - roughness) * (0.45 + (0.55 * ao));
    }else{
        envContrib *= mix(0.85, 1.65, metallic);
    }
    if(u_useEnvMap != 0 || localProbeInfluence > 1e-4){
        vec3 envSample = vec3(0.0);
        if(u_useEnvMap != 0){
            envSample = sampleEnvironmentSpecular(R, roughness);
        }
        if(localProbeInfluence > 1e-4){
            vec3 localProbeSample = sampleLocalProbeSpecular(v_fragPos, R, roughness);
            envSample = (u_useEnvMap != 0)
                ? mix(envSample, localProbeSample, localProbeInfluence)
                : localProbeSample;
        }
        envSpec = envSample * Fenv * envContrib;
    }
    if(bsdfModel == 1 || bsdfModel == 2){
        envSpec *= 0.18;
    }
    if(bsdfModel == 0){
        float planarEnvWeight = 0.0;
        vec3 planarEnvSample = samplePlanarReflection(v_fragPos, R, roughness, planarEnvWeight);
        if(planarEnvWeight > 1e-4){
            vec3 planarSpec = planarEnvSample * Fenv * envContrib * max(u_planarReflectionStrength, 0.0);
            envSpec = (u_useEnvMap != 0)
                ? mix(envSpec, planarSpec, planarEnvWeight)
                : (planarSpec * planarEnvWeight);
        }
    }
    vec3 normalView = safeNormalize(mat3(u_view) * N);
    vec3 surfaceViewPos = (u_view * vec4(v_fragPos, 1.0)).xyz;
    vec3 viewIncident = safeNormalize(surfaceViewPos);
    bool hasSceneColor = (u_useSceneColor != 0);
    bool hasSceneDepth = (u_useSceneDepth != 0);
    vec2 sceneTexSize = vec2(0.0);
    vec2 sceneTexelSize = vec2(0.0);
    vec2 sceneUv = vec2(0.5);
    vec2 refractedUv = vec2(0.5);
    vec3 sceneBaseColor = vec3(0.0);
    vec3 refractedSceneColor = vec3(0.0);
    float sceneDepthBehind = 0.0;
    float refractionTravelDistance = 0.0;
    float mediumDepth = 0.0;
    vec3 volumeTransmittance = vec3(1.0);
    float volumeExtinction = 0.0;
    if(hasSceneColor){
        sceneTexSize = vec2(textureSize(u_ssrColor, 0));
        hasSceneColor = (sceneTexSize.x > 1.0 && sceneTexSize.y > 1.0);
        if(hasSceneColor){
            sceneTexelSize = 1.0 / max(sceneTexSize, vec2(1.0));
            sceneUv = gl_FragCoord.xy / sceneTexSize;
            refractedUv = clampUv(sceneUv);
            sceneBaseColor = texture(u_ssrColor, clampUv(sceneUv)).rgb;
            refractedSceneColor = sceneBaseColor;
        }
    }
    if(hasSceneDepth){
        vec2 sceneDepthTexSize = vec2(textureSize(u_sceneDepth, 0));
        hasSceneDepth = (sceneDepthTexSize.x > 1.0 && sceneDepthTexSize.y > 1.0);
    }

    if((bsdfModel == 1 || bsdfModel == 2) && hasSceneColor && hasSceneDepth){
        float straightSceneDepth = texture(u_sceneDepth, clampUv(sceneUv)).r;
        if(straightSceneDepth < 0.999999){
            vec3 straightScenePos = reconstructSceneViewPosition(clampUv(sceneUv));
            float straightDepthDelta = max(surfaceViewPos.z - straightScenePos.z, 0.0);
            if(straightDepthDelta > 1e-4){
                vec3 refractionNormalView = normalView;
                if(dot(refractionNormalView, -viewIncident) < 0.0){
                    refractionNormalView = -refractionNormalView;
                }

                vec3 refractedViewDir = refract(viewIncident, refractionNormalView, 1.0 / max(materialIor, 1.0001));
                if(length(refractedViewDir) <= 1e-4){
                    refractedViewDir = reflect(viewIncident, refractionNormalView);
                }

                float projectedTravel = straightDepthDelta / max(-refractedViewDir.z, 0.15);
                vec3 refractedHit = surfaceViewPos + (refractedViewDir * projectedTravel);
                vec2 projectedUv = projectViewPositionToUv(refractedHit);
                if(isUvInBounds(projectedUv)){
                    float projectedDepth = texture(u_sceneDepth, projectedUv).r;
                    if(projectedDepth < 0.999999){
                        vec3 projectedScenePos = reconstructSceneViewPosition(projectedUv);
                        float projectedDepthDelta = max(surfaceViewPos.z - projectedScenePos.z, 0.0);
                        projectedTravel = projectedDepthDelta / max(-refractedViewDir.z, 0.15);
                        refractedHit = surfaceViewPos + (refractedViewDir * projectedTravel);
                        projectedUv = projectViewPositionToUv(refractedHit);
                    }
                }

                if(isUvInBounds(projectedUv)){
                    refractedUv = clampUv(projectedUv);
                }

                float refractedDepth = texture(u_sceneDepth, refractedUv).r;
                if(refractedDepth < 0.999999){
                    vec3 refractedScenePos = reconstructSceneViewPosition(refractedUv);
                    sceneDepthBehind = max(surfaceViewPos.z - refractedScenePos.z, 0.0);
                    refractionTravelDistance = max(length(refractedScenePos - surfaceViewPos), straightDepthDelta);
                }else{
                    sceneDepthBehind = straightDepthDelta;
                    refractionTravelDistance = straightDepthDelta;
                }

                float refractionBlur = min(
                    1.0,
                    roughness + ((1.0 - NdotV) * 0.12) + ((bsdfModel == 2) ? 0.06 : 0.03)
                );
                refractedSceneColor = sampleSceneColorRough(
                    u_ssrColor,
                    refractedUv,
                    refractionBlur,
                    sceneTexelSize
                );
            }
        }
    }

    if(bsdfModel == 1){
        mediumDepth = max(u_thickness, 0.001) / max(NdotV, 0.18);
    }else if(bsdfModel == 2){
        mediumDepth = max(refractionTravelDistance * max(u_thickness, 0.001), 0.0);
    }
    volumeTransmittance = mix(
        vec3(1.0),
        computeVolumeTransmittance(u_attenuationColor, u_attenuationDistance, mediumDepth),
        transmission
    );
    volumeExtinction = clamp(1.0 - dot(volumeTransmittance, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);

    vec3 reflectionCompositeColor = vec3(0.0);
    float reflectionCompositeAmount = 0.0;
    if(bsdfModel == 1 || bsdfModel == 2){
        vec3 transmissiveFresnel = FresnelSchlick(NdotV, vec3(dielectricReflectance));
        float fresnelAmount = clamp(
            max(max(transmissiveFresnel.r, transmissiveFresnel.g), transmissiveFresnel.b),
            0.0,
            1.0
        );
        vec3 reflectionTint = (bsdfModel == 1)
            ? mix(vec3(1.0), bsdfAlbedo, 0.025)
            : mix(vec3(1.0), bsdfAlbedo, 0.060);

        vec3 reflectionSample = vec3(0.0);
        float reflectionWeight = 0.0;
        bool hit = false;
        if(u_useSsr != 0 &&
           hasSceneColor &&
           hasSceneDepth &&
           roughness <= max(u_ssrRoughnessCutoff, 0.05)){
            vec3 reflectView = safeNormalize(reflect(viewIncident, normalView));
            if(reflectView.z < -0.005){
                vec2 hitUv = vec2(0.0);
                float hitDistance = 0.0;
                hit = traceSceneDepthRay(
                    surfaceViewPos + (normalView * max(u_ssrThickness * 0.35, 0.003)),
                    reflectView,
                    hash12(gl_FragCoord.xy) * clamp(u_ssrJitter, 0.0, 1.0),
                    hitUv,
                    hitDistance
                );
                if(hit){
                    reflectionSample = sampleSceneColorRough(u_ssrColor, hitUv, roughness, sceneTexelSize);
                    reflectionWeight = 1.0 - smoothstep(u_ssrMaxDistance * 0.35, u_ssrMaxDistance, hitDistance);
                    float hitEdge = min(min(hitUv.x, hitUv.y), min(1.0 - hitUv.x, 1.0 - hitUv.y));
                    reflectionWeight *= smoothstep(0.0, max(u_ssrEdgeFade, 1e-4), hitEdge);
                }
            }
        }

        if(reflectionWeight <= 1e-4 && u_usePlanarReflection != 0){
            float planarReflectionWeight = 0.0;
            vec3 planarReflectionSample = samplePlanarReflection(v_fragPos, R, roughness, planarReflectionWeight);
            if(planarReflectionWeight > 1e-4){
                reflectionSample = planarReflectionSample;
                reflectionWeight = planarReflectionWeight * max(u_planarReflectionStrength, 0.0);
            }
        }

        if(reflectionWeight <= 1e-4 && (u_useEnvMap != 0 || localProbeInfluence > 1e-4)){
            if(u_useEnvMap != 0){
                reflectionSample = sampleEnvironmentSpecular(reflect(-V, N), roughness);
            }
            if(localProbeInfluence > 1e-4){
                vec3 localProbeSample = sampleLocalProbeSpecular(v_fragPos, reflect(-V, N), roughness);
                reflectionSample = (u_useEnvMap != 0)
                    ? mix(reflectionSample, localProbeSample, localProbeInfluence)
                    : localProbeSample;
            }
            reflectionWeight = mix(0.28, 0.62, 1.0 - roughness);
        }

        if(reflectionWeight > 1e-4){
            float screenEdge = hasSceneColor
                ? min(min(sceneUv.x, sceneUv.y), min(1.0 - sceneUv.x, 1.0 - sceneUv.y))
                : 1.0;
            float screenEdgeFade = hasSceneColor
                ? smoothstep(0.0, max(u_ssrEdgeFade, 1e-4), screenEdge)
                : 1.0;
            float reflectionAmount = reflectionWeight * fresnelAmount * screenEdgeFade;
            if(u_useSsr != 0 && hit){
                reflectionAmount *= clamp(u_ssrIntensity, 0.0, 4.0);
            }
            reflectionCompositeColor = reflectionSample * reflectionTint;
            reflectionCompositeAmount = clamp(reflectionAmount, 0.0, 0.97);
        }
    }
    vec3 emissive = u_emissiveColor * u_emissiveStrength;
    if(u_useEmissiveTex != 0){
        emissive *= textureGrad(u_emissiveTex, uv, duvDx, duvDy).rgb;
    }

    vec3 surfaceColor = ambient + Lo + envSpec + emissive;
    vec3 volumeScatter = vec3(0.0);
    if(bsdfModel == 1){
        vec3 glassTint = mix(vec3(1.0), bsdfAlbedo, 0.10);
        vec3 bodyTint = glassTint * (0.018 + (baseColor.a * 0.14) + (min(u_thickness, 1.0) * 0.05));
        volumeScatter = (glassTint * (1.0 - volumeTransmittance) * (0.06 + (u_scatteringStrength * 0.18))) + bodyTint;
    }else if(bsdfModel == 2){
        vec3 shallowTint = mix(vec3(0.86, 0.97, 1.0), bsdfAlbedo, 0.18);
        vec3 deepTint = mix(shallowTint, bsdfAlbedo, clamp(volumeExtinction + 0.18, 0.0, 1.0));
        float scatterAmount = clamp(u_scatteringStrength, 0.0, 4.0) * (0.18 + (0.42 * (1.0 - roughness)));
        volumeScatter = deepTint * (1.0 - volumeTransmittance) * scatterAmount;
    }
    vec3 color = surfaceColor + volumeScatter;
    if(debugWeight > 0.0){
        FragColor = vec4(clamp(debugColorAccum / debugWeight, 0.0, 1.0), 1.0);
    }else{
        float outAlpha = baseColor.a;
        if(bsdfModel == 1){
            outAlpha = clamp(
                max(
                    (baseColor.a * 0.60) + 0.04,
                    reflectionCompositeAmount + (volumeExtinction * 0.70) + ((1.0 - transmission) * 0.28) + (min(u_thickness, 1.0) * 0.05)
                ),
                0.14,
                0.76
            );
        }else if(bsdfModel == 2){
            outAlpha = clamp(
                max((baseColor.a * 0.30) + 0.03, (reflectionCompositeAmount * 0.78) + (volumeExtinction * 0.92) + ((1.0 - transmission) * 0.20)),
                0.08,
                0.94
            );
        }

        if((bsdfModel == 1 || bsdfModel == 2) && hasSceneColor){
            vec3 transmittedScene = refractedSceneColor * volumeTransmittance;
            vec3 layeredScene = transmittedScene;
            if(reflectionCompositeAmount > 1e-4){
                layeredScene = mix(transmittedScene, reflectionCompositeColor, reflectionCompositeAmount);
            }

            float surfaceOverlayStrength = (bsdfModel == 1)
                ? mix(0.16, 0.42, 1.0 - NdotV)
                : mix(0.10, 0.28, 1.0 - NdotV);
            surfaceOverlayStrength *= mix(1.0, 0.82, roughness);

            vec3 desiredComposite = layeredScene + (surfaceColor * surfaceOverlayStrength) + volumeScatter;
            if(bsdfModel == 2){
                desiredComposite += bsdfAlbedo * pow(1.0 - NdotV, 2.0) * clamp(u_scatteringStrength, 0.0, 4.0) * 0.03;
            }

            // Convert the desired composite back into a straight-alpha source color so
            // standard alpha blending still lands close to the intended refracted result.
            vec3 solvedColor =
                (desiredComposite - (sceneBaseColor * (1.0 - outAlpha))) /
                max(outAlpha, 1e-4);
            FragColor = vec4(max(solvedColor, vec3(0.0)), outAlpha);
            return;
        }

        if(bsdfModel == 1 || bsdfModel == 2){
            color += reflectionCompositeColor * reflectionCompositeAmount;
        }
        FragColor = vec4(color, outAlpha);
    }
}




