#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aTexCoord;
layout (location = 4) in vec4 aTangent;
        
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform int u_useUserClipPlane;
uniform vec4 u_userClipPlane;
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
uniform sampler2D u_heightTex;
uniform int u_useHeightTex;
uniform vec2 u_uvScale;
uniform vec2 u_uvOffset;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;
out vec4 v_tangent;

vec2 safeNormalize2(vec2 v){
    float lenV = length(v);
    return (lenV > 1e-5) ? (v / lenV) : vec2(0.8, 0.6);
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

float sampleWaterWave(vec3 localPos, vec2 uv, float timeSec){
    vec2 wavePos = localPos.xz;
    return sampleProceduralWave(wavePos, timeSec) + sampleTextureWave(uv, timeSec);
}
    
void main() {
    vec3 localPos = aPos;
    vec3 localNormal = normalize(aNormal);
    vec2 uvBase = aTexCoord * u_uvScale + u_uvOffset;
    bool applyWaterDisplacement =
        (u_bsdfModel == 2) &&
        (u_enableWaveDisplacement != 0) &&
        (u_waveAmplitude > 1e-5);

    if(applyWaterDisplacement){
        float centerHeight = sampleWaterWave(localPos, uvBase, u_time) * u_waveAmplitude;
        localPos.y += centerHeight;

        float eps = 0.05;
        float heightX = sampleWaterWave(
            localPos + vec3(eps, 0.0, 0.0),
            uvBase + vec2(eps * 0.15, 0.0),
            u_time
        ) * u_waveAmplitude;
        float heightZ = sampleWaterWave(
            localPos + vec3(0.0, 0.0, eps),
            uvBase + vec2(0.0, eps * 0.15),
            u_time
        ) * u_waveAmplitude;

        vec3 tangentX = vec3(eps, heightX - centerHeight, 0.0);
        vec3 tangentZ = vec3(0.0, heightZ - centerHeight, eps);
        vec3 waveNormal = normalize(cross(tangentZ, tangentX));
        if(dot(waveNormal, localNormal) < 0.0){
            waveNormal = -waveNormal;
        }
        localNormal = normalize(mix(localNormal, waveNormal, 0.85));
    }

    mat3 normalMatrix = mat3(transpose(inverse(u_model)));
    vec4 worldPos4 = u_model * vec4(localPos, 1.0);
    v_fragPos = worldPos4.xyz;
    v_normal = normalize(normalMatrix * localNormal);
    vec3 tangentWs = normalMatrix * aTangent.xyz;
    if(length(tangentWs) <= 1e-5){
        vec3 up = (abs(v_normal.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        tangentWs = normalize(cross(up, normalize(v_normal)));
    }else{
        tangentWs = normalize(tangentWs);
    }
    v_tangent = vec4(tangentWs, aTangent.w);
    v_uv = aTexCoord;
    v_color = aColor;
    gl_ClipDistance[0] = (u_useUserClipPlane != 0) ? dot(worldPos4, u_userClipPlane) : 1.0;
    gl_Position = u_projection * u_view * worldPos4;
}
