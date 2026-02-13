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
            float bias = light.shadow.x + light.shadow.y * (1.0 - max(dot(norm, safeNormalize(light.direction.xyz)), 0.0));
            float visibility = 1.0;
            int lType = int(light.meta.x + 0.5);
            int sType = int(light.meta.y + 0.5);
            int baseIndex = int(light.meta.z + 0.5);
            int cascadeCount = 1;
            int cascadeIndex = 0;

            if(lType == 0){
                visibility = sampleShadowCube(sType, baseIndex, v_fragPos, light.position.xyz, max(light.cascadeSplits.x, 0.1), bias);
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


