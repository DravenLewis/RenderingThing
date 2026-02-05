#version 410 core

#define MAX_LIGHTS 8

struct Light {
    int type;            // 0 = Point, 1 = Directional, 2 = Spot
    vec3 position;
    vec3 direction;
    vec4 color;
    float intensity;
    float range;
    float falloff;
    float spotAngle;
};
    
in vec3 v_fragPos;
in vec3 v_normal; // Ignored in favor of geometric normal
in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;

uniform sampler2D u_texture;
uniform vec4 u_color;
uniform vec3 u_viewPos;
uniform Light u_lights[MAX_LIGHTS];
uniform int u_lightCount;

// Same lighting function as LitImage, but logic is separated for clarity
vec3 calculateLight(Light light, vec3 norm, vec3 viewDir, vec3 fragPos, vec4 texColor) {
    vec3 result = vec3(0.0);
    
    if (light.type == 0) { // Point
        vec3 lightDir = normalize(light.position - fragPos);
        float distance = length(light.position - fragPos);
        float attenuation = 0.0;
        if (distance < light.range) {
            attenuation = 1.0 / pow(max(distance, 0.1), light.falloff);
            attenuation = clamp(attenuation, 0.0, 1.0);
        }
        
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * light.color.rgb * texColor.rgb * attenuation;
        
        float specularStrength = 0.2; // Reduced for matte/flat look
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = specularStrength * spec * light.color.rgb * attenuation;
        
        result = (diffuse + specular) * light.intensity;
        
    } else if (light.type == 1) { // Directional
        vec3 lightDir = normalize(-light.direction);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * light.color.rgb * texColor.rgb;
        
        float specularStrength = 0.2;
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = specularStrength * spec * light.color.rgb;
        
        result = (diffuse + specular) * light.intensity;
        
    } else if (light.type == 2) { // Spot
        vec3 lightDir = normalize(light.position - fragPos);
        float distance = length(light.position - fragPos);
        float attenuation = 0.0;
        if (distance < light.range) {
            float theta = degrees(acos(dot(-lightDir, normalize(light.direction))));
            if (theta < light.spotAngle) {
                attenuation = 1.0 / pow(max(distance, 0.1), light.falloff);
                attenuation = clamp(attenuation, 0.0, 1.0);
            }
        }
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * light.color.rgb * texColor.rgb * attenuation;
        
        float specularStrength = 0.2;
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
        vec3 specular = specularStrength * spec * light.color.rgb * attenuation;
        
        result = (diffuse + specular) * light.intensity;
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
    for (int i = 0; i < u_lightCount && i < MAX_LIGHTS; i++) {
        totalLight += calculateLight(u_lights[i], norm, viewDir, v_fragPos, texColor);
    }
    
    vec3 result = ambient + totalLight;
    FragColor = vec4(result, texColor.a * u_color.a);
}
