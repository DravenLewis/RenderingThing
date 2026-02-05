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
in vec3 v_normal; // Ignored
in vec2 v_uv;
in vec4 v_color;

out vec4 FragColor;
    
uniform vec4 u_color;
uniform vec3 u_viewPos;

layout(std140) uniform LightBlock {
    vec4 u_lightHeader;  // x=lightCount
    Light u_lights[MAX_LIGHTS];
};

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
            attenuation = 1.0 / pow(max(distance, 0.1), light.params.z);
            attenuation = clamp(attenuation, 0.0, 1.0);
        } else {
            attenuation = 0.0;
        }
    } else if (lightType == 1) { // Directional
        lightDir = normalize(-light.direction.xyz);
    } else if (lightType == 2) { // Spot
        lightDir = normalize(light.position.xyz - fragPos);
        float distance = length(light.position.xyz - fragPos);
        if (distance < light.params.y) {
            float theta = degrees(acos(dot(-lightDir, normalize(light.direction.xyz))));
            if (theta < light.params.w) {
                attenuation = 1.0 / pow(max(distance, 0.1), light.params.z);
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
    int lightCount = int(u_lightHeader.x + 0.5);
    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        totalLight += calculateLight(u_lights[i], norm, v_fragPos);
    }
    
    vec3 result = ambient + totalLight;
    
    // Simple Tone Mapping (Optional)
    // prevents colors from going above 1.0 immediately, helpful for bright lights
    // result = result / (result + vec3(1.0)); 

    FragColor = vec4(result, u_color.a);
}
