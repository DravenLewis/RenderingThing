#version 410 core

#define MAX_LIGHTS 16
#define MAX_SHADOW_MAPS_2D 16
#define MAX_SHADOW_MAPS_CUBE 2

struct Light {
    vec4 meta;           // x=type, y=shadowType, z=shadowMapIndex, w=shadowStrength
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;         // x=intensity, y=range, z=falloff, w=spotAngle
    vec4 shadow;         // x=bias, y=normalBias
    vec4 cascadeSplits;  // view-space split distances
    mat4 lightMatrices[4];   // directional cascades / spot uses [0]
};
    
in vec3 v_fragPos;
in vec3 v_normal; // Ignored
in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;
    
uniform vec4 u_color;
uniform vec3 u_viewPos;
uniform mat4 u_view;
uniform int u_receiveShadows;
uniform int u_debugShadows; // 0=off,1=visibility,2=cascade index,3=proj bounds

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;  // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

uniform sampler2DShadow u_shadowMaps2D[MAX_SHADOW_MAPS_2D];
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

mat2 rotate2D(float angle){
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c, -s, s, c);
}


float sampleShadow2D(int shadowType, int mapIndex, vec4 lightSpacePos, float bias) {
    if(mapIndex < 0 || mapIndex >= MAX_SHADOW_MAPS_2D) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    if(projCoords.z > 1.001 || projCoords.z < -0.001) return 1.0;
    if(projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 1.0;

    vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMaps2D[mapIndex], 0));
    float filterScale = 1.0;
    vec2 pcfTexel = texelSize * filterScale;
    float pcfBias = 0.0;
    if(shadowType == 1){
        pcfBias = max(texelSize.x, texelSize.y) * filterScale * 0.90;
    }else if(shadowType == 2){
        pcfBias = max(texelSize.x, texelSize.y) * filterScale * 1.60;
    }
    float compareDepth = clamp(projCoords.z - (bias + pcfBias), 0.0, 1.0);
    vec2 uvMin = texelSize * 1.5;
    vec2 uvMax = vec2(1.0) - uvMin;

    if(shadowType == 0){
        return texture(u_shadowMaps2D[mapIndex], vec3(clamp(projCoords.xy, uvMin, uvMax), compareDepth));
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
            vec2 sampleUv = clamp(projCoords.xy + (kernel[i] * pcfTexel * 1.15), uvMin, uvMax);
            float weight = 1.0 - smoothstep(0.75, 1.10, length(kernel[i]));
            shadow += texture(u_shadowMaps2D[mapIndex], vec3(sampleUv, compareDepth)) * weight;
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
            vec2 sampleUv = clamp(projCoords.xy + (kernel[i] * pcfTexel * 1.85), uvMin, uvMax);
            float weight = 1.0 - smoothstep(0.70, 1.15, length(kernel[i]));
            shadow += texture(u_shadowMaps2D[mapIndex], vec3(sampleUv, compareDepth)) * weight;
            weightSum += weight;
        }
    }

    float visibility = shadow / max(weightSum, 0.0001);
    float edge = min(min(projCoords.x, projCoords.y), min(1.0 - projCoords.x, 1.0 - projCoords.y));
    float edgeFade = smoothstep(0.0, max(texelSize.x, texelSize.y) * 8.0, edge);
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
        float baseBias = baseOffset * 0.010;
        float slopeBias = normalOffset * (0.0018 + slope * 0.0080);
        return clamp(baseBias + slopeBias, 0.00008, 0.00070);
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
        float worldOffset = normalOffset * 0.030 + baseOffset * 0.010;
        worldOffset = clamp(worldOffset, 0.0, 0.00035);
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

vec3 calculateLight(Light light, vec3 norm, vec3 fragPos) {
    vec3 result = vec3(0.0);
    
    // Direction & Attenuation Logic
    vec3 lightDir = vec3(0.0);
    float attenuation = 1.0;

    int lightType = int(light.meta.x + 0.5);

    if (lightType == 0) { // Point
        lightDir = normalize(light.position.xyz - fragPos);
        float distance = length(light.position.xyz - fragPos);
        if (distance < light.params.y) {
            float range = max(light.params.y, 0.001);
            float d = clamp(distance / range, 0.0, 1.0);
            float falloff = clamp(light.params.z, 0.001, 3.0);
            attenuation = pow(1.0 - d, falloff);
        } else {
            attenuation = 0.0;
        }
    } else if (lightType == 1) { // Directional
        lightDir = normalize(-light.direction.xyz);
    } else if (lightType == 2) { // Spot
        lightDir = normalize(light.position.xyz - fragPos);
        float distance = length(light.position.xyz - fragPos);
        if (distance < light.params.y) {
            float cosTheta = clamp(dot(-lightDir, safeNormalize(light.direction.xyz)), -1.0, 1.0);
            float theta = degrees(acos(cosTheta));
            if (theta < light.params.w) {
                float range = max(light.params.y, 0.001);
                float d = clamp(distance / range, 0.0, 1.0);
                float falloff = clamp(light.params.z, 0.001, 3.0);
                attenuation = pow(1.0 - d, falloff);
            } else {
                attenuation = 0.0;
            }
        } else {
            attenuation = 0.0;
        }
    }

    // --- DIFFUSE ONLY (Matte Look) ---
    // We removed the specular calculation here.
    float diff = max(dot(norm, lightDir), 0.0);
    
    // Multiply by light color and material color
    vec3 diffuse = diff * light.color.rgb * u_color.rgb;
    
    result = diffuse * attenuation * light.params.x;
    return result;
}

void main() {
    // 1. Calculate Face Normal (Flat Look)
    vec3 xTangent = dFdx(v_fragPos);
    vec3 yTangent = dFdy(v_fragPos);
    vec3 norm = normalize(cross(xTangent, yTangent));

    // 2. Base Ambient (Soft shadows)
    // Increased to 0.5 to match the lighter shadows in your reference
    float ambientStrength = 0.5; 
    vec3 ambient = ambientStrength * u_color.rgb;
    
    // 3. Add up lights
    vec3 totalLight = vec3(0.0);
    float debugVisibility = 1.0;
    int lightCount = int(u_lightHeader.x + 0.5);
    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        Light light = u_lights[i];
        vec3 lightContribution = calculateLight(light, norm, v_fragPos);

        if(u_receiveShadows != 0 && light.meta.z >= 0.0){
            float bias = computeShadowBias(light, norm, v_fragPos);
            vec3 shadowPos = offsetShadowReceiver(light, norm, v_fragPos);
            float visibility = 1.0;
            int lType = int(light.meta.x + 0.5);
            int sType = int(light.meta.y + 0.5);
            int baseIndex = int(light.meta.z + 0.5);
            int cascadeCount = 1;
            int cascadeIndex = 0;

            if(lType == 0){
                visibility = sampleShadowCube(sType, baseIndex, shadowPos, light.position.xyz, max(light.cascadeSplits.x, 0.1), bias);
            }else{
                cascadeCount = clamp(int(light.shadow.z + 0.5), 1, 4);
                float viewDepth = -(u_view * vec4(shadowPos, 1.0)).z;
                if(cascadeCount > 1){
                    if(viewDepth > light.cascadeSplits.x) cascadeIndex = 1;
                    if(viewDepth > light.cascadeSplits.y) cascadeIndex = 2;
                    if(viewDepth > light.cascadeSplits.z) cascadeIndex = 3;
                    cascadeIndex = clamp(cascadeIndex, 0, cascadeCount - 1);
                }
                if(lType == 1){
                    float cascadeBiasScale = (sType == 0) ? 0.02 : 0.05;
                    bias *= (1.0 + float(cascadeIndex) * cascadeBiasScale);
                }
                vec4 lightSpacePos = light.lightMatrices[cascadeIndex] * vec4(shadowPos, 1.0);
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
                if(lType == 1 && cascadeCount > 1 && cascadeIndex == (cascadeCount - 1) && sType != 0){
                    float prevSplit = (cascadeIndex > 0) ? light.cascadeSplits[cascadeIndex - 1] : 0.0;
                    float splitDist = light.cascadeSplits[cascadeIndex];
                    float cascadeSpan = max(splitDist - prevSplit, 0.001);
                    float localCascadeT = clamp((viewDepth - prevSplit) / cascadeSpan, 0.0, 1.0);
                    float hardVisibility = sampleShadow2D(0, baseIndex + cascadeIndex, lightSpacePos, bias);
                    float filterToHard = smoothstep(0.25, 0.95, localCascadeT);
                    visibility = mix(visibility, hardVisibility, filterToHard);
                }
                if(lType == 1 && cascadeCount > 1 && cascadeIndex < (cascadeCount - 1)){
                    float splitDist = light.cascadeSplits[cascadeIndex];
                    float prevSplit = (cascadeIndex > 0) ? light.cascadeSplits[cascadeIndex - 1] : 0.0;
                    float cascadeSpan = max(splitDist - prevSplit, 0.001);
                    float blendRange = clamp(cascadeSpan * 0.22, 0.20, 2.50);
                    float blendStart = splitDist - blendRange;
                    float blendT = clamp((viewDepth - blendStart) / blendRange, 0.0, 1.0);
                    if(blendT > 0.0){
                        vec4 nextLightSpacePos = light.lightMatrices[cascadeIndex + 1] * vec4(shadowPos, 1.0);
                        float nextVisibility = sampleShadow2D(sType, baseIndex + cascadeIndex + 1, nextLightSpacePos, bias);
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
            debugVisibility = min(debugVisibility, visibility);
            if(u_debugShadows == 2 && lType != 0){
                float t = (cascadeCount > 1) ? (float(cascadeIndex) / float(max(cascadeCount - 1, 1))) : 0.0;
                FragColor = vec4(vec3(t), 1.0);
                return;
            }
        }

        totalLight += lightContribution;
    }
    
    vec3 result = ambient + totalLight;
    
    // Simple Tone Mapping (Optional)
    // prevents colors from going above 1.0 immediately, helpful for bright lights
    // result = result / (result + vec3(1.0)); 

    if(u_debugShadows == 1){
        FragColor = vec4(vec3(debugVisibility), 1.0);
    }else if(u_debugShadows == 2){
        FragColor = vec4(1.0);
    }else if(u_debugShadows == 3){
        FragColor = vec4(1.0);
    }else{
        FragColor = vec4(result, u_color.a);
    }
}




