#version 410 core

#define MAX_LIGHTS 8
#define MAX_SHADOW_MAPS_2D 4
#define MAX_SHADOW_MAPS_CUBE 2

struct Light {
    vec4 meta;           // x=type, y=shadowType, z=shadowMapIndex, w=shadowStrength
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;         // x=intensity, y=range, z=falloff, w=spotAngle
    vec4 shadow;         // x=bias, y=normalBias
    mat4 lightMatrix;    // directional/spot
};
    
in vec3 v_fragPos;
in vec3 v_normal; // Ignored in favor of geometric normal
in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;

uniform sampler2D u_texture;
uniform vec4 u_color;
uniform vec3 u_viewPos;
uniform int u_receiveShadows;

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;  // x=lightCount
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

// Same lighting function as LitImage, but logic is separated for clarity
vec3 calculateLight(Light light, vec3 norm, vec3 viewDir, vec3 fragPos, vec4 texColor) {
    vec3 result = vec3(0.0);
    
    int lightType = int(light.meta.x + 0.5);

    if (lightType == 0) { // Point
        vec3 lightDir = normalize(light.position.xyz - fragPos);
        float distance = length(light.position.xyz - fragPos);
        float attenuation = 0.0;
        if (distance < light.params.y) {
            attenuation = 1.0 / pow(max(distance, 0.1), light.params.z);
            attenuation = clamp(attenuation, 0.0, 1.0);
        }
        
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * light.color.rgb * texColor.rgb * attenuation;
        
        float specularStrength = 0.2; // Reduced for matte/flat look
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = specularStrength * spec * light.color.rgb * attenuation;
        
        result = (diffuse + specular) * light.params.x;
        
    } else if (lightType == 1) { // Directional
        vec3 lightDir = normalize(-light.direction.xyz);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * light.color.rgb * texColor.rgb;
        
        float specularStrength = 0.2;
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = specularStrength * spec * light.color.rgb;
        
        result = (diffuse + specular) * light.params.x;
        
    } else if (lightType == 2) { // Spot
        vec3 lightDir = normalize(light.position.xyz - fragPos);
        float distance = length(light.position.xyz - fragPos);
        float attenuation = 0.0;
        if (distance < light.params.y) {
            float theta = degrees(acos(dot(-lightDir, normalize(light.direction.xyz))));
            if (theta < light.params.w) {
                attenuation = 1.0 / pow(max(distance, 0.1), light.params.z);
                attenuation = clamp(attenuation, 0.0, 1.0);
            }
        }
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * light.color.rgb * texColor.rgb * attenuation;
        
        float specularStrength = 0.2;
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = specularStrength * spec * light.color.rgb * attenuation;
        
        result = (diffuse + specular) * light.params.x;
    }
    
    return result;
}

void main() {
    vec4 texColor = texture(u_texture, v_uv);
    
    // --- FLAT SHADING CALCULATION ---
    vec3 xTangent = dFdx(v_fragPos);
    vec3 yTangent = dFdy(v_fragPos);
    vec3 norm = normalize(cross(xTangent, yTangent));
    // --------------------------------
    
    vec3 viewDir = normalize(u_viewPos - v_fragPos);
    
    // Ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * texColor.rgb;
    
    vec3 totalLight = vec3(0.0);
    int lightCount = int(u_lightHeader.x + 0.5);
    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        Light light = u_lights[i];
        vec3 lightContribution = calculateLight(light, norm, viewDir, v_fragPos, texColor);

        if(u_receiveShadows != 0 && light.meta.z >= 0.0){
            float bias = light.shadow.x + light.shadow.y * (1.0 - max(dot(norm, normalize(light.direction.xyz)), 0.0));
            float visibility = 1.0;
            int lType = int(light.meta.x + 0.5);
            int sType = int(light.meta.y + 0.5);
            int sIndex = int(light.meta.z + 0.5);

            if(lType == 0){
                visibility = sampleShadowCube(sType, sIndex, v_fragPos, light.position.xyz, light.params.y, bias);
            }else{
                visibility = sampleShadow2D(sType, sIndex, light.lightMatrix * vec4(v_fragPos, 1.0), bias);
            }

            visibility = mix(1.0, visibility, clamp(light.meta.w, 0.0, 1.0));
            lightContribution *= visibility;
        }

        totalLight += lightContribution;
    }
    
    vec3 result = ambient + totalLight;
    FragColor = vec4(result, texColor.a * u_color.a);
}
