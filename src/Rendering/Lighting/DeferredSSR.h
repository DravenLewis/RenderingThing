#ifndef DEFERREDSSR_H
#define DEFERREDSSR_H

#include <array>
#include <memory>
#include <string>

#include "Foundation/Logging/Logbot.h"
#include "Foundation/Math/Math3D.h"
#include "Rendering/Core/FrameBuffer.h"
#include "Rendering/Core/Graphics.h"
#include "Rendering/Geometry/ModelPart.h"
#include "Rendering/Shaders/ShaderProgram.h"
#include "Rendering/Textures/CubeMap.h"
#include "Rendering/Textures/Texture.h"

struct DeferredSSRSettings {
    bool enabled = true;
    float intensity = 0.85f;
    float maxDistance = 80.0f;
    float thickness = 0.18f;
    float stride = 0.75f;
    float jitter = 0.35f;
    int maxSteps = 56;
    float roughnessCutoff = 0.82f;
    float edgeFade = 0.18f;
    bool useCameraReflectionCache = false;
    int cameraReflectionUpdateInterval = 2;
    float cameraReflectionInfluenceRadius = 18.0f;
};

class DeferredSSR {
    private:
        std::shared_ptr<ShaderProgram> compositeShader;
        bool compileAttempted = false;
        PFrameBuffer compositeFbo = nullptr;

        const std::string SSR_COMPOSITE_FRAG_SHADER = R"(
            #version 330 core

            out vec4 FragColor;
            in vec2 TexCoords;

            uniform sampler2D u_sceneColor;
            uniform sampler2D u_wideSceneColor;
            uniform samplerCube u_localProbe;
            uniform sampler2D u_albedoTexture;
            uniform sampler2D u_normalTexture;
            uniform sampler2D u_depthTexture;
            uniform sampler2D u_surfaceTexture;
            uniform samplerCube u_envMap;
            uniform sampler2D u_planarReflectionTex;
            uniform int u_useEnvMap;
            uniform int u_useWideScene;
            uniform int u_useLocalProbe;
            uniform int u_usePlanarReflection;

            uniform mat4 u_projMatrix;
            uniform mat4 u_wideProjMatrix;
            uniform mat4 u_invProjMatrix;
            uniform mat4 u_viewMatrix;
            uniform mat4 u_invViewMatrix;
            uniform mat4 u_planarReflectionMatrix;
            uniform vec3 u_localProbeCenter;
            uniform vec3 u_localProbeCaptureMin;
            uniform vec3 u_localProbeCaptureMax;
            uniform vec3 u_localProbeInfluenceMin;
            uniform vec3 u_localProbeInfluenceMax;
            uniform vec3 u_planarReflectionCenter;
            uniform vec3 u_planarReflectionNormal;
            uniform float u_planarReflectionStrength;
            uniform float u_planarReflectionReceiverFadeDistance;

            uniform vec2 u_texelSize;
            uniform int u_maxSteps;
            uniform float u_stride;
            uniform float u_maxDistance;
            uniform float u_thickness;
            uniform float u_jitter;
            uniform float u_intensity;
            uniform float u_roughnessCutoff;
            uniform float u_edgeFade;

            vec3 safeNormalize(vec3 v){
                float lenV = length(v);
                return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, 0.0, 1.0);
            }

            float hash12(vec2 p){
                vec3 p3 = fract(vec3(p.xyx) * 0.1031);
                p3 += dot(p3, p3.yzx + 33.33);
                return fract((p3.x + p3.y) * p3.z);
            }

            vec3 reconstructViewPosition(vec2 uv){
                float depth = texture(u_depthTexture, uv).r;
                vec4 clip = vec4((uv * 2.0) - 1.0, (depth * 2.0) - 1.0, 1.0);
                vec4 view = u_invProjMatrix * clip;
                if(abs(view.w) <= 1e-5){
                    return vec3(0.0, 0.0, -1.0);
                }
                return view.xyz / view.w;
            }

            vec3 viewToWorldDirection(vec3 viewDir){
                return safeNormalize((u_invViewMatrix * vec4(viewDir, 0.0)).xyz);
            }

            float maxComponent(vec3 v){
                return max(max(v.x, v.y), v.z);
            }

            vec2 clampUv(vec2 uv){
                return clamp(uv, vec2(0.001), vec2(0.999));
            }

            vec3 sampleSceneColorRough(vec2 uv, float roughness){
                uv = clampUv(uv);
                float blur = clamp(roughness * roughness, 0.0, 1.0);
                if(blur <= 1e-4){
                    return texture(u_sceneColor, uv).rgb;
                }

                float radius = mix(0.65, 6.0, blur);
                vec2 axisX = vec2(u_texelSize.x * radius, 0.0);
                vec2 axisY = vec2(0.0, u_texelSize.y * radius);
                vec2 diag = u_texelSize * radius * 0.70710678;

                vec3 color = texture(u_sceneColor, uv).rgb * 0.28;
                color += texture(u_sceneColor, clampUv(uv + axisX)).rgb * 0.11;
                color += texture(u_sceneColor, clampUv(uv - axisX)).rgb * 0.11;
                color += texture(u_sceneColor, clampUv(uv + axisY)).rgb * 0.11;
                color += texture(u_sceneColor, clampUv(uv - axisY)).rgb * 0.11;
                color += texture(u_sceneColor, clampUv(uv + diag)).rgb * 0.07;
                color += texture(u_sceneColor, clampUv(uv + vec2(-diag.x, diag.y))).rgb * 0.07;
                color += texture(u_sceneColor, clampUv(uv + vec2(diag.x, -diag.y))).rgb * 0.07;
                color += texture(u_sceneColor, clampUv(uv - diag)).rgb * 0.07;
                return color;
            }

            vec3 sampleWideSceneColorRough(vec2 uv, float roughness){
                uv = clampUv(uv);
                float blur = clamp(roughness * roughness, 0.0, 1.0);
                if(blur <= 1e-4){
                    return texture(u_wideSceneColor, uv).rgb;
                }

                float radius = mix(0.65, 6.0, blur);
                vec2 axisX = vec2(u_texelSize.x * radius, 0.0);
                vec2 axisY = vec2(0.0, u_texelSize.y * radius);
                vec2 diag = u_texelSize * radius * 0.70710678;

                vec3 color = texture(u_wideSceneColor, uv).rgb * 0.28;
                color += texture(u_wideSceneColor, clampUv(uv + axisX)).rgb * 0.11;
                color += texture(u_wideSceneColor, clampUv(uv - axisX)).rgb * 0.11;
                color += texture(u_wideSceneColor, clampUv(uv + axisY)).rgb * 0.11;
                color += texture(u_wideSceneColor, clampUv(uv - axisY)).rgb * 0.11;
                color += texture(u_wideSceneColor, clampUv(uv + diag)).rgb * 0.07;
                color += texture(u_wideSceneColor, clampUv(uv + vec2(-diag.x, diag.y))).rgb * 0.07;
                color += texture(u_wideSceneColor, clampUv(uv + vec2(diag.x, -diag.y))).rgb * 0.07;
                color += texture(u_wideSceneColor, clampUv(uv - diag)).rgb * 0.07;
                return color;
            }

            vec3 sampleCubeColorRough(samplerCube cubeMap, vec3 worldDir, float roughness){
                float baseSize = float(max(textureSize(cubeMap, 0).x, 1));
                float maxMip = max(log2(baseSize), 0.0);
                float lod = clamp((roughness * roughness) * maxMip, 0.0, maxMip);
                return textureLod(cubeMap, safeNormalize(worldDir), lod).rgb;
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

            vec3 parallaxCorrectLocalProbeDir(vec3 worldPos, vec3 worldDir){
                vec3 dir = safeNormalize(worldDir);
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

            vec3 sampleLocalProbeColorRough(vec3 worldPos, vec3 worldDir, float roughness){
                return sampleCubeColorRough(
                    u_localProbe,
                    parallaxCorrectLocalProbeDir(worldPos, worldDir),
                    roughness
                );
            }

            vec3 sampleEnvironmentColorRough(vec3 worldDir, float roughness){
                return sampleCubeColorRough(u_envMap, worldDir, roughness);
            }

            vec3 samplePlanarReflection(vec3 worldPos, vec3 worldReflectDir, float roughness, out float weight){
                weight = 0.0;
                if(u_usePlanarReflection == 0){
                    return vec3(0.0);
                }

                vec2 texSize = vec2(textureSize(u_planarReflectionTex, 0));
                if(texSize.x <= 1.0 || texSize.y <= 1.0){
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

                float hitEpsilon = mix(0.015, 0.08, clamp(roughness, 0.0, 1.0));
                float hitT = -planeDistance / planeDenom;
                if(hitT < -hitEpsilon){
                    return vec3(0.0);
                }

                vec3 hitPos = worldPos + (reflectDir * max(hitT, 0.0));
                vec4 reflectedClip = u_planarReflectionMatrix * vec4(hitPos, 1.0);
                if(reflectedClip.w <= 1e-5){
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
                return textureLod(u_planarReflectionTex, clampUv(uv), lod).rgb;
            }

            bool traceSsrRay(vec3 originView,
                             vec3 dirView,
                             float jitter,
                             out vec2 outHitUv,
                             out float outHitDistance,
                             out vec2 outWideUv,
                             out float outWideDistance){
                float stride = max(u_stride, 0.25);
                float thicknessBase = max(u_thickness, 0.005);
                float traveled = 0.0;
                outWideUv = vec2(-1.0);
                outWideDistance = 0.0;
                vec4 startClip = u_projMatrix * vec4(originView, 1.0);
                if(abs(startClip.w) <= 1e-5){
                    return false;
                }

                vec2 startUv = (startClip.xy / startClip.w) * 0.5 + 0.5;
                vec2 minScreenStep = u_texelSize * 1.0;
                float minTravel = max(stride * 0.35, thicknessBase * 1.25);
                float prevGap = -1e9;
                vec3 prevRayPos = originView;
                bool hasPrev = false;
                vec3 rayPos = originView + (dirView * max(stride * mix(0.10, 0.65, jitter), 0.015));
                int stepCount = clamp(u_maxSteps, 8, 256);
                for(int i = 0; i < stepCount; ++i){
                    traveled = length(rayPos - originView);
                    if(traveled > u_maxDistance){
                        break;
                    }

                    vec4 clip = u_projMatrix * vec4(rayPos, 1.0);
                    if(abs(clip.w) <= 1e-5){
                        break;
                    }

                    vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
                    if(uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0){
                        if(u_useWideScene != 0){
                            vec4 wideClip = u_wideProjMatrix * vec4(rayPos, 1.0);
                            if(wideClip.w > 1e-5){
                                vec2 wideUv = (wideClip.xy / wideClip.w) * 0.5 + 0.5;
                                if(wideUv.x > 0.0 && wideUv.x < 1.0 && wideUv.y > 0.0 && wideUv.y < 1.0){
                                    outWideUv = clampUv(wideUv);
                                    outWideDistance = traveled;
                                }
                            }
                        }
                        break;
                    }

                    if((abs(uv.x - startUv.x) <= minScreenStep.x) &&
                       (abs(uv.y - startUv.y) <= minScreenStep.y) &&
                       traveled < minTravel){
                        rayPos += dirView * stride;
                        continue;
                    }

                    float sceneDepth = texture(u_depthTexture, uv).r;
                    if(sceneDepth >= 0.999999){
                        hasPrev = false;
                        rayPos += dirView * stride;
                        continue;
                    }

                    vec3 scenePosView = reconstructViewPosition(uv);
                    float gap = scenePosView.z - rayPos.z;
                    float pixelThickness = max(u_texelSize.x, u_texelSize.y) * max(abs(rayPos.z), 1.0) * 1.5;
                    float thickness = (thicknessBase * mix(1.0, 2.6, clamp(traveled / max(u_maxDistance, 0.001), 0.0, 1.0))) + pixelThickness;
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
                            vec4 midClip = u_projMatrix * vec4(mid, 1.0);
                            if(abs(midClip.w) <= 1e-5){
                                break;
                            }

                            vec2 midUv = (midClip.xy / midClip.w) * 0.5 + 0.5;
                            if(midUv.x <= 0.0 || midUv.x >= 1.0 || midUv.y <= 0.0 || midUv.y >= 1.0){
                                break;
                            }

                            float midDepth = texture(u_depthTexture, midUv).r;
                            if(midDepth >= 0.999999){
                                refineA = mid;
                                continue;
                            }

                            vec3 midScenePos = reconstructViewPosition(midUv);
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

            bool gatherNeighborMissReflection(vec2 baseUv,
                                              vec3 baseViewPos,
                                              vec3 baseNormalView,
                                              float roughness,
                                              out vec3 outColor,
                                              out float outWeight){
                const vec2 offsets[8] = vec2[8](
                    vec2( 1.0,  0.0),
                    vec2(-1.0,  0.0),
                    vec2( 0.0,  1.0),
                    vec2( 0.0, -1.0),
                    vec2( 1.0,  1.0),
                    vec2(-1.0,  1.0),
                    vec2( 1.0, -1.0),
                    vec2(-1.0, -1.0)
                );

                float neighborRadius = mix(8.0, 4.0, clamp(roughness * 3.5, 0.0, 1.0));
                float depthThreshold = max(u_thickness * 8.0, 0.08 + (abs(baseViewPos.z) * 0.05));
                vec3 accumColor = vec3(0.0);
                float accumWeight = 0.0;

                for(int i = 0; i < 8; ++i){
                    vec2 neighborUv = clampUv(baseUv + (offsets[i] * u_texelSize * neighborRadius));
                    vec4 neighborNormalData = texture(u_normalTexture, neighborUv);
                    if(length(neighborNormalData.xyz) <= 1e-4){
                        continue;
                    }

                    vec3 neighborViewPos = reconstructViewPosition(neighborUv);
                    float depthDelta = abs(neighborViewPos.z - baseViewPos.z);
                    if(depthDelta > depthThreshold){
                        continue;
                    }

                    vec3 neighborNormalView = safeNormalize(mat3(u_viewMatrix) * safeNormalize(neighborNormalData.xyz));
                    float normalAgreement = clamp(dot(neighborNormalView, baseNormalView), 0.0, 1.0);
                    if(normalAgreement <= 0.25){
                        continue;
                    }

                    vec3 neighborReflectDir = safeNormalize(reflect(safeNormalize(neighborViewPos), neighborNormalView));
                    vec2 neighborHitUv = vec2(0.0);
                    float neighborHitDistance = 0.0;
                    vec2 neighborWideUv = vec2(-1.0);
                    float neighborWideDistance = 0.0;
                    bool neighborHit = traceSsrRay(
                        neighborViewPos + (neighborNormalView * max(u_thickness * 0.30, 0.003)),
                        neighborReflectDir,
                        fract(hash12(gl_FragCoord.xy + (offsets[i] * 19.17)) + (float(i) * 0.173)),
                        neighborHitUv,
                        neighborHitDistance,
                        neighborWideUv,
                        neighborWideDistance
                    );

                    vec3 sampleColor = vec3(0.0);
                    float sampleWeight = 0.0;
                    if(neighborHit){
                        sampleColor = sampleSceneColorRough(neighborHitUv, roughness);
                        sampleWeight = 1.0 - smoothstep(u_maxDistance * 0.35, u_maxDistance, neighborHitDistance);
                    }else if(u_useWideScene != 0 && neighborWideDistance > 0.0){
                        sampleColor = sampleWideSceneColorRough(neighborWideUv, roughness);
                        sampleWeight = 0.28 * (1.0 - smoothstep(u_maxDistance * 0.30, u_maxDistance * 0.95, neighborWideDistance));
                    }

                    if(sampleWeight <= 1e-4){
                        continue;
                    }

                    float screenDistWeight = 1.0 / (1.0 + length(offsets[i]));
                    float depthWeight = 1.0 - smoothstep(0.0, depthThreshold, depthDelta);
                    float weight = sampleWeight * normalAgreement * screenDistWeight * depthWeight;
                    accumColor += sampleColor * weight;
                    accumWeight += weight;
                }

                if(accumWeight <= 1e-4){
                    return false;
                }

                outColor = accumColor / accumWeight;
                outWeight = clamp(accumWeight * 2.1, 0.0, 1.0);
                return true;
            }

            void main(){
                vec3 sceneColor = texture(u_sceneColor, TexCoords).rgb;
                vec4 normalData = texture(u_normalTexture, TexCoords);
                if(length(normalData.xyz) <= 1e-4){
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                float packedMode = normalData.w;
                bool legacySurface = (packedMode < 0.0);
                int bsdfModel = 0;
                float metallic = 0.0;
                if(!legacySurface){
                    bsdfModel = clamp(int(floor((packedMode + 1e-4) * 0.5)), 0, 2);
                    metallic = clamp(packedMode - (float(bsdfModel) * 2.0), 0.0, 1.0);
                }
                vec4 surfaceData = texture(u_surfaceTexture, TexCoords);
                vec3 albedo = max(texture(u_albedoTexture, TexCoords).rgb, vec3(0.0));
                float roughness = clamp(surfaceData.r, 0.04, 1.0);
                float transmission = clamp(surfaceData.a, 0.0, 1.0);

                float roughnessFade = 1.0 - smoothstep(u_roughnessCutoff, 1.0, roughness);
                if(roughnessFade <= 1e-4){
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                vec3 bsdfAlbedo = albedo;
                if(bsdfModel == 2){
                    bsdfAlbedo = mix(albedo, vec3(0.06, 0.32, 0.45), 0.35);
                }
                vec3 F0 = legacySurface ? vec3(0.06) : mix(vec3(0.04), bsdfAlbedo, clamp(metallic, 0.0, 1.0));
                vec3 reflectionTint = vec3(1.0);
                if(bsdfModel == 1){
                    metallic = 0.0;
                    F0 = mix(vec3(0.04), bsdfAlbedo, 0.06);
                    reflectionTint = mix(vec3(1.0), bsdfAlbedo, 0.08);
                }else if(bsdfModel == 2){
                    metallic = 0.0;
                    F0 = mix(vec3(0.02), bsdfAlbedo, 0.04);
                    reflectionTint = mix(vec3(1.0), bsdfAlbedo, 0.12);
                }else if(!legacySurface){
                    vec3 metallicTint = mix(vec3(1.0), clamp(bsdfAlbedo, vec3(0.0), vec3(1.0)), 0.18);
                    reflectionTint = mix(vec3(1.0), metallicTint, clamp(metallic, 0.0, 1.0));
                }

                float reflectivity = clamp(maxComponent(F0), 0.02, 1.0);
                float reflectionBoost = 1.0;
                if(bsdfModel == 1){
                    reflectionBoost = 1.10;
                }else if(bsdfModel == 2){
                    reflectionBoost = 0.92;
                }else if(!legacySurface){
                    float metalMirror = clamp(metallic * (1.0 - (roughness * 0.45)), 0.0, 1.0);
                    reflectionBoost = mix(0.82, 1.45, metalMirror);
                }
                reflectionBoost *= mix(0.92, 1.02, clamp(surfaceData.b, 0.0, 1.0));

                vec3 normalView = safeNormalize(mat3(u_viewMatrix) * safeNormalize(normalData.xyz));
                vec3 viewPos = reconstructViewPosition(TexCoords);
                vec3 worldPos = (u_invViewMatrix * vec4(viewPos, 1.0)).xyz;
                vec3 viewDirToCamera = safeNormalize(-viewPos);
                float viewDistance = length(viewPos);
                float localProbeInfluence = computeLocalProbeInfluence(worldPos);
                float standardSurfaceFade = 1.0;
                if(bsdfModel == 0){
                    standardSurfaceFade =
                        1.0 -
                        (smoothstep(0.22, 0.70, roughness) *
                         (1.0 - clamp(metallic, 0.0, 1.0)) *
                         smoothstep(u_maxDistance * 0.10, u_maxDistance * 0.60, viewDistance));
                    standardSurfaceFade = clamp(standardSurfaceFade, 0.0, 1.0);
                }
                vec3 incident = safeNormalize(viewPos);
                vec3 reflectDir = safeNormalize(reflect(incident, normalView));
                vec3 worldReflectDir = viewToWorldDirection(reflectDir);
                bool allowSmoothMetalNeighborFill =
                    (bsdfModel == 0) &&
                    (clamp(metallic, 0.0, 1.0) > 0.72) &&
                    (roughness < 0.22);
                if(reflectDir.z >= -0.005){
                    if(allowSmoothMetalNeighborFill){
                        vec3 rescuedColor = vec3(0.0);
                        float rescuedWeight = 0.0;
                        if(gatherNeighborMissReflection(TexCoords, viewPos, normalView, roughness, rescuedColor, rescuedWeight)){
                            float NdotV = clamp(dot(normalView, viewDirToCamera), 0.0, 1.0);
                            float fresnel = pow(1.0 - NdotV, 5.0);
                            float fresnelAmount = reflectivity + ((1.0 - reflectivity) * fresnel);
                            float edge = min(min(TexCoords.x, TexCoords.y), min(1.0 - TexCoords.x, 1.0 - TexCoords.y));
                            float edgeFade = smoothstep(0.0, max(u_edgeFade, 1e-4), edge);
                            float amount =
                                u_intensity *
                                reflectionBoost *
                                mix(0.40, 0.92, rescuedWeight) *
                                fresnelAmount *
                                roughnessFade *
                                edgeFade *
                                standardSurfaceFade;
                            vec3 rescuedReflection = rescuedColor * reflectionTint;
                            FragColor = vec4(mix(sceneColor, rescuedReflection, clamp(amount, 0.0, 1.0)), 1.0);
                            return;
                        }
                    }

                    if(u_usePlanarReflection != 0){
                        float planarWeight = 0.0;
                        vec3 planarReflection = samplePlanarReflection(worldPos, worldReflectDir, roughness, planarWeight);
                        if(planarWeight > 1e-4){
                            float NdotV = clamp(dot(normalView, viewDirToCamera), 0.0, 1.0);
                            float fresnel = pow(1.0 - NdotV, 5.0);
                            float fresnelAmount = reflectivity + ((1.0 - reflectivity) * fresnel);
                            float edge = min(min(TexCoords.x, TexCoords.y), min(1.0 - TexCoords.x, 1.0 - TexCoords.y));
                            float edgeFade = smoothstep(0.0, max(u_edgeFade, 1e-4), edge);
                            float amount =
                                u_intensity *
                                reflectionBoost *
                                (planarWeight * max(u_planarReflectionStrength, 0.0)) *
                                fresnelAmount *
                                roughnessFade *
                                edgeFade;
                            if(bsdfModel == 0){
                                amount *= standardSurfaceFade;
                            }else{
                                amount *= mix(0.82, 1.08, transmission);
                            }
                            FragColor = vec4(mix(sceneColor, planarReflection * reflectionTint, clamp(amount, 0.0, 1.0)), 1.0);
                            return;
                        }
                    }

                    // Reflection vector points out of the screen; fall back to env only.
                    if(localProbeInfluence > 1e-4 || u_useEnvMap != 0){
                        vec3 envColor = vec3(0.0);
                        if(u_useEnvMap != 0){
                            envColor = sampleEnvironmentColorRough(worldReflectDir, roughness) * reflectionTint;
                        }
                        if(localProbeInfluence > 1e-4){
                            vec3 localProbeColor = sampleLocalProbeColorRough(worldPos, worldReflectDir, roughness) * reflectionTint;
                            envColor = (u_useEnvMap != 0)
                                ? mix(envColor, localProbeColor, localProbeInfluence)
                                : localProbeColor;
                        }
                        float NdotV = clamp(dot(normalView, viewDirToCamera), 0.0, 1.0);
                        float fresnel = pow(1.0 - NdotV, 5.0);
                        float fresnelAmount = reflectivity + ((1.0 - reflectivity) * fresnel);
                        float edge = min(min(TexCoords.x, TexCoords.y), min(1.0 - TexCoords.x, 1.0 - TexCoords.y));
                        float edgeFade = smoothstep(0.0, max(u_edgeFade, 1e-4), edge);
                        float amount = u_intensity * reflectionBoost * fresnelAmount * roughnessFade * edgeFade;
                        if(bsdfModel == 0){
                            float roughFarFade =
                                1.0 -
                                (smoothstep(0.22, 0.70, roughness) *
                                 (1.0 - clamp(metallic, 0.0, 1.0)) *
                                 smoothstep(u_maxDistance * 0.10, u_maxDistance * 0.60, viewDistance));
                            amount *= roughFarFade * standardSurfaceFade;
                        }
                        FragColor = vec4(mix(sceneColor, envColor, clamp(amount, 0.0, 1.0)), 1.0);
                    }else{
                        FragColor = vec4(sceneColor, 1.0);
                    }
                    return;
                }

                float jitter = hash12(gl_FragCoord.xy) * clamp(u_jitter, 0.0, 1.0);
                vec2 hitUv = vec2(0.0);
                float hitDistance = 0.0;
                vec2 wideUv = vec2(-1.0);
                float wideDistance = 0.0;
                bool hit = traceSsrRay(
                    viewPos + (normalView * max(u_thickness * 0.35, 0.003)),
                    reflectDir,
                    jitter,
                    hitUv,
                    hitDistance,
                    wideUv,
                    wideDistance
                );

                vec3 reflectionColor = vec3(0.0);
                float reflectionWeight = 0.0;
                if(hit){
                    reflectionColor = sampleSceneColorRough(hitUv, roughness) * reflectionTint;
                    reflectionWeight = 1.0 - smoothstep(u_maxDistance * 0.35, u_maxDistance, hitDistance);
                }else{
                    if(allowSmoothMetalNeighborFill){
                        vec3 rescuedColor = vec3(0.0);
                        float rescuedWeight = 0.0;
                        if(gatherNeighborMissReflection(TexCoords, viewPos, normalView, roughness, rescuedColor, rescuedWeight)){
                            reflectionColor = rescuedColor * reflectionTint;
                            reflectionWeight = mix(0.40, 0.92, rescuedWeight);
                        }
                    }

                    float localFallbackWeight = (bsdfModel == 0)
                        ? mix(0.34, 0.82, clamp((reflectivity * 0.78) + ((1.0 - roughness) * 0.22), 0.0, 1.0))
                        : 0.70;
                    float envFallbackWeight = (bsdfModel == 0)
                        ? mix(0.24, 0.76, clamp((reflectivity * 0.78) + ((1.0 - roughness) * 0.22), 0.0, 1.0))
                        : 0.62;
                    if(reflectionWeight <= 1e-4 && u_usePlanarReflection != 0){
                        float planarWeight = 0.0;
                        vec3 planarReflection = samplePlanarReflection(worldPos, worldReflectDir, roughness, planarWeight);
                        if(planarWeight > 1e-4){
                            reflectionColor = planarReflection * reflectionTint;
                            reflectionWeight = planarWeight * max(u_planarReflectionStrength, 0.0);
                        }
                    }

                    if(reflectionWeight <= 1e-4 && (localProbeInfluence > 1e-4 || u_useEnvMap != 0)){
                        if(u_useEnvMap != 0){
                            reflectionColor = sampleEnvironmentColorRough(worldReflectDir, roughness) * reflectionTint;
                        }
                        if(localProbeInfluence > 1e-4){
                            vec3 localProbeColor = sampleLocalProbeColorRough(worldPos, worldReflectDir, roughness) * reflectionTint;
                            reflectionColor = (u_useEnvMap != 0)
                                ? mix(reflectionColor, localProbeColor, localProbeInfluence)
                                : localProbeColor;
                            reflectionWeight = (u_useEnvMap != 0)
                                ? mix(envFallbackWeight, localFallbackWeight, localProbeInfluence)
                                : localFallbackWeight;
                        }else{
                            reflectionWeight = envFallbackWeight;
                        }
                    }

                    bool allowWideFallback =
                        (bsdfModel != 0) ||
                        (clamp(metallic, 0.0, 1.0) > 0.45) ||
                        (viewDistance < (u_maxDistance * 0.25) && roughness < 0.28);
                    if(reflectionWeight <= 1e-4 &&
                       u_useWideScene != 0 &&
                       wideDistance > 0.0 &&
                       localProbeInfluence <= 1e-4 &&
                       allowWideFallback){
                        vec3 wideColor = sampleWideSceneColorRough(wideUv, roughness) * reflectionTint;
                        float wideWeight = 0.40 * (1.0 - smoothstep(u_maxDistance * 0.30, u_maxDistance * 0.95, wideDistance));
                        float wideEdge = min(min(wideUv.x, wideUv.y), min(1.0 - wideUv.x, 1.0 - wideUv.y));
                        wideWeight *= smoothstep(0.0, max(u_edgeFade * 1.5, 1e-4), wideEdge);
                        if(reflectionWeight > 1e-4){
                            float wideBlend = clamp(wideWeight * 0.75, 0.0, 0.60);
                            reflectionColor = mix(reflectionColor, wideColor, wideBlend);
                            reflectionWeight = max(reflectionWeight, wideWeight);
                        }else{
                            reflectionColor = wideColor;
                            reflectionWeight = wideWeight;
                        }
                    }

                    if(bsdfModel == 0 && reflectionWeight > 0.0){
                        float roughFarFade =
                            1.0 -
                            (smoothstep(0.22, 0.70, roughness) *
                             (1.0 - clamp(metallic, 0.0, 1.0)) *
                             smoothstep(u_maxDistance * 0.10, u_maxDistance * 0.60, viewDistance));
                        reflectionWeight *= roughFarFade * standardSurfaceFade;
                    }
                }

                if(reflectionWeight <= 1e-4){
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                float NdotV = clamp(dot(normalView, viewDirToCamera), 0.0, 1.0);
                float fresnel = pow(1.0 - NdotV, 5.0);
                float fresnelAmount = reflectivity + ((1.0 - reflectivity) * fresnel);
                float edge = min(min(TexCoords.x, TexCoords.y), min(1.0 - TexCoords.x, 1.0 - TexCoords.y));
                float edgeFade = smoothstep(0.0, max(u_edgeFade, 1e-4), edge);
                float amount = u_intensity * reflectionBoost * reflectionWeight * fresnelAmount * roughnessFade * edgeFade;
                if(bsdfModel == 1 || bsdfModel == 2){
                    amount *= mix(0.82, 1.08, transmission);
                }else{
                    amount *= standardSurfaceFade;
                }
                amount = clamp(amount, 0.0, 1.0);

                vec3 color = mix(sceneColor, reflectionColor, amount);
                FragColor = vec4(max(color, vec3(0.0)), 1.0);
            }
        )";

        bool ensureCompiled(){
            if(!compositeShader){
                return false;
            }
            if(compositeShader->getID() != 0){
                return true;
            }
            if(compileAttempted){
                return false;
            }
            compileAttempted = true;
            if(compositeShader->compile() == 0){
                LogBot.Log(LOG_ERRO, "Failed to compile deferred SSR shader:\n%s", compositeShader->getLog().c_str());
                return false;
            }
            return true;
        }

        static void configureTextureFilter(PTexture texture, GLint filter){
            if(!texture || texture->getID() == 0){
                return;
            }
            glBindTexture(GL_TEXTURE_2D, texture->getID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        bool ensureTarget(int width, int height){
            if(width <= 0 || height <= 0){
                return false;
            }
            if(compositeFbo &&
               compositeFbo->getWidth() == width &&
               compositeFbo->getHeight() == height &&
               compositeFbo->getTexture()){
                configureTextureFilter(compositeFbo->getTexture(), GL_LINEAR);
                return true;
            }

            compositeFbo = FrameBuffer::Create(width, height);
            if(!compositeFbo){
                return false;
            }
            compositeFbo->attachTexture(Texture::CreateRenderTarget(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT));
            configureTextureFilter(compositeFbo->getTexture(), GL_LINEAR);
            return compositeFbo->getTexture() && compositeFbo->validate();
        }

        void drawFullscreenPass(const std::shared_ptr<ShaderProgram>& shaderProgram, const std::shared_ptr<ModelPart>& quad){
            static const Math3D::Mat4 IDENTITY;
            shaderProgram->setUniformFast("u_model", Uniform<Math3D::Mat4>(IDENTITY));
            shaderProgram->setUniformFast("u_view", Uniform<Math3D::Mat4>(IDENTITY));
            shaderProgram->setUniformFast("u_projection", Uniform<Math3D::Mat4>(IDENTITY));
            quad->draw(IDENTITY, IDENTITY, IDENTITY);
        }

    public:
        DeferredSSR(){
            compositeShader = std::make_shared<ShaderProgram>();
            compositeShader->setVertexShader(Graphics::ShaderDefaults::SCREEN_VERT_SRC);
            compositeShader->setFragmentShader(SSR_COMPOSITE_FRAG_SHADER);
        }

        bool renderComposite(int width,
                             int height,
                             const std::shared_ptr<ModelPart>& quad,
                             PTexture sceneColorTexture,
                             PTexture wideSceneColorTexture,
                             PCubeMap localProbeMap,
                             const Math3D::Vec3& localProbeCenter,
                             const Math3D::Vec3& localProbeCaptureMin,
                             const Math3D::Vec3& localProbeCaptureMax,
                             const Math3D::Vec3& localProbeInfluenceMin,
                             const Math3D::Vec3& localProbeInfluenceMax,
                             PTexture planarReflectionTexture,
                             const Math3D::Mat4& planarReflectionMatrix,
                             const Math3D::Vec3& planarReflectionCenter,
                             const Math3D::Vec3& planarReflectionNormal,
                             float planarReflectionStrength,
                             float planarReflectionReceiverFadeDistance,
                             PTexture albedoTexture,
                             PTexture normalTexture,
                             PTexture depthTexture,
                             PTexture surfaceTexture,
                             const Math3D::Mat4& viewMatrix,
                             const Math3D::Mat4& projectionMatrix,
                             const Math3D::Mat4& wideProjectionMatrix,
                             PCubeMap envMap,
                             const DeferredSSRSettings& settings){
            if(width <= 0 || height <= 0 || !quad || !sceneColorTexture || !albedoTexture || !normalTexture || !depthTexture || !surfaceTexture){
                return false;
            }
            if(!settings.enabled){
                return false;
            }
            if(!ensureCompiled() || !ensureTarget(width, height)){
                return false;
            }

            configureTextureFilter(sceneColorTexture, GL_LINEAR);
            configureTextureFilter(wideSceneColorTexture, GL_LINEAR);

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            compositeFbo->bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            compositeShader->bind();
            const bool useLocalProbe = localProbeMap && localProbeMap->getID() != 0;
            const bool usePlanarReflection = planarReflectionTexture && planarReflectionTexture->getID() != 0;
            compositeShader->setUniformFast("u_sceneColor", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(sceneColorTexture, 0)));
            compositeShader->setUniformFast("u_wideSceneColor", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(wideSceneColorTexture, 1)));
            compositeShader->setUniformFast("u_localProbe", Uniform<GLUniformUpload::CubeMapSlot>(GLUniformUpload::CubeMapSlot(localProbeMap, 2)));
            compositeShader->setUniformFast("u_albedoTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(albedoTexture, 3)));
            compositeShader->setUniformFast("u_normalTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(normalTexture, 4)));
            compositeShader->setUniformFast("u_depthTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(depthTexture, 5)));
            compositeShader->setUniformFast("u_surfaceTexture", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(surfaceTexture, 6)));
            compositeShader->setUniformFast("u_envMap", Uniform<GLUniformUpload::CubeMapSlot>(GLUniformUpload::CubeMapSlot(envMap, 7)));
            compositeShader->setUniformFast("u_planarReflectionTex", Uniform<GLUniformUpload::TextureSlot>(GLUniformUpload::TextureSlot(planarReflectionTexture, 8)));
            compositeShader->setUniformFast("u_useEnvMap", Uniform<int>(envMap ? 1 : 0));
            compositeShader->setUniformFast("u_useWideScene", Uniform<int>(wideSceneColorTexture ? 1 : 0));
            compositeShader->setUniformFast("u_useLocalProbe", Uniform<int>(useLocalProbe ? 1 : 0));
            compositeShader->setUniformFast("u_usePlanarReflection", Uniform<int>(usePlanarReflection ? 1 : 0));
            compositeShader->setUniformFast("u_projMatrix", Uniform<Math3D::Mat4>(projectionMatrix));
            compositeShader->setUniformFast("u_wideProjMatrix", Uniform<Math3D::Mat4>(wideProjectionMatrix));
            compositeShader->setUniformFast("u_invProjMatrix", Uniform<Math3D::Mat4>(Math3D::Mat4(glm::inverse(glm::mat4(projectionMatrix)))));
            compositeShader->setUniformFast("u_viewMatrix", Uniform<Math3D::Mat4>(viewMatrix));
            compositeShader->setUniformFast("u_invViewMatrix", Uniform<Math3D::Mat4>(Math3D::Mat4(glm::inverse(glm::mat4(viewMatrix)))));
            compositeShader->setUniformFast("u_planarReflectionMatrix", Uniform<Math3D::Mat4>(planarReflectionMatrix));
            compositeShader->setUniformFast("u_localProbeCenter", Uniform<Math3D::Vec3>(localProbeCenter));
            compositeShader->setUniformFast("u_localProbeCaptureMin", Uniform<Math3D::Vec3>(localProbeCaptureMin));
            compositeShader->setUniformFast("u_localProbeCaptureMax", Uniform<Math3D::Vec3>(localProbeCaptureMax));
            compositeShader->setUniformFast("u_localProbeInfluenceMin", Uniform<Math3D::Vec3>(localProbeInfluenceMin));
            compositeShader->setUniformFast("u_localProbeInfluenceMax", Uniform<Math3D::Vec3>(localProbeInfluenceMax));
            compositeShader->setUniformFast("u_planarReflectionCenter", Uniform<Math3D::Vec3>(planarReflectionCenter));
            compositeShader->setUniformFast("u_planarReflectionNormal", Uniform<Math3D::Vec3>(planarReflectionNormal));
            compositeShader->setUniformFast("u_planarReflectionStrength", Uniform<float>(Math3D::Max(planarReflectionStrength, 0.0f)));
            compositeShader->setUniformFast(
                "u_planarReflectionReceiverFadeDistance",
                Uniform<float>(Math3D::Max(planarReflectionReceiverFadeDistance, 0.05f))
            );
            compositeShader->setUniformFast("u_texelSize", Uniform<Math3D::Vec2>(Math3D::Vec2(
                1.0f / static_cast<float>(width),
                1.0f / static_cast<float>(height)
            )));
            compositeShader->setUniformFast("u_maxSteps", Uniform<int>(Math3D::Clamp(settings.maxSteps, 8, 256)));
            compositeShader->setUniformFast("u_stride", Uniform<float>(Math3D::Clamp(settings.stride, 0.1f, 8.0f)));
            compositeShader->setUniformFast("u_maxDistance", Uniform<float>(Math3D::Clamp(settings.maxDistance, 0.5f, 2500.0f)));
            compositeShader->setUniformFast("u_thickness", Uniform<float>(Math3D::Clamp(settings.thickness, 0.005f, 4.0f)));
            compositeShader->setUniformFast("u_jitter", Uniform<float>(Math3D::Clamp(settings.jitter, 0.0f, 1.0f)));
            compositeShader->setUniformFast("u_intensity", Uniform<float>(Math3D::Clamp(settings.intensity, 0.0f, 4.0f)));
            compositeShader->setUniformFast("u_roughnessCutoff", Uniform<float>(Math3D::Clamp(settings.roughnessCutoff, 0.05f, 1.0f)));
            compositeShader->setUniformFast("u_edgeFade", Uniform<float>(Math3D::Clamp(settings.edgeFade, 0.001f, 0.5f)));
            drawFullscreenPass(compositeShader, quad);
            compositeFbo->unbind();
            return true;
        }

        PTexture getCompositeTexture() const{
            return compositeFbo ? compositeFbo->getTexture() : nullptr;
        }

        PFrameBuffer getCompositeBuffer() const{
            return compositeFbo;
        }
};

#endif // DEFERREDSSR_H
