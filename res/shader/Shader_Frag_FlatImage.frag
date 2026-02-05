#version 410 core

#define MAX_LIGHTS 8

struct Light {
    vec4 meta;           // x=type
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;         // x=intensity, y=range, z=falloff, w=spotAngle
};
    
in vec3 v_fragPos;
in vec3 v_normal; // Ignored in favor of geometric normal
in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;

uniform sampler2D u_texture;
uniform vec4 u_color;
uniform vec3 u_viewPos;

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;  // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

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
        totalLight += calculateLight(u_lights[i], norm, viewDir, v_fragPos, texColor);
    }
    
    vec3 result = ambient + totalLight;
    FragColor = vec4(result, texColor.a * u_color.a);
}
