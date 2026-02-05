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
in vec3 v_normal; // Ignored
in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;
    
uniform vec4 u_color;
uniform Light u_lights[MAX_LIGHTS];
uniform int u_lightCount;
uniform vec3 u_viewPos;

vec3 calculateLight(Light light, vec3 norm, vec3 fragPos) {
    vec3 result = vec3(0.0);
    
    // Direction & Attenuation Logic
    vec3 lightDir = vec3(0.0);
    float attenuation = 1.0;

    if (light.type == 0) { // Point
        lightDir = normalize(light.position - fragPos);
        float distance = length(light.position - fragPos);
        if (distance < light.range) {
            attenuation = 1.0 / pow(max(distance, 0.1), light.falloff);
            attenuation = clamp(attenuation, 0.0, 1.0);
        } else {
            attenuation = 0.0;
        }
    } else if (light.type == 1) { // Directional
        lightDir = normalize(-light.direction);
    } else if (light.type == 2) { // Spot
        lightDir = normalize(light.position - fragPos);
        float distance = length(light.position - fragPos);
        if (distance < light.range) {
            float theta = degrees(acos(dot(-lightDir, normalize(light.direction))));
            if (theta < light.spotAngle) {
                attenuation = 1.0 / pow(max(distance, 0.1), light.falloff);
                attenuation = clamp(attenuation, 0.0, 1.0);
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
    
    result = diffuse * attenuation * light.intensity;
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
    for (int i = 0; i < u_lightCount && i < MAX_LIGHTS; i++) {
        totalLight += calculateLight(u_lights[i], norm, v_fragPos);
    }
    
    vec3 result = ambient + totalLight;
    
    // Simple Tone Mapping (Optional)
    // prevents colors from going above 1.0 immediately, helpful for bright lights
    // result = result / (result + vec3(1.0)); 

    FragColor = vec4(result, u_color.a);
}