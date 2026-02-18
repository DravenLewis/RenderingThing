#version 410 core

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gPosition;

in vec3 v_fragPos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_baseColor;
uniform sampler2D u_baseColorTex;
uniform int u_useBaseColorTex;

uniform float u_metallic;
uniform float u_roughness;
uniform sampler2D u_metallicRoughnessTex;
uniform int u_useMetallicRoughnessTex;

uniform sampler2D u_normalTex;
uniform int u_useNormalTex;
uniform float u_normalScale;

uniform sampler2D u_occlusionTex;
uniform int u_useOcclusionTex;
uniform float u_aoStrength;

uniform vec2 u_uvScale;
uniform vec2 u_uvOffset;

uniform int u_useAlphaClip;
uniform float u_alphaCutoff;
uniform int u_surfaceMode; // 0=PBR, 1=LegacyLit, 2=LegacyUnlit

vec3 safeNormalize(vec3 v){
    float lenV = length(v);
    return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, 1.0, 0.0);
}

vec3 getNormal(vec2 uv){
    vec3 N = safeNormalize(v_normal);
    if(u_useNormalTex == 0){
        return N;
    }

    vec3 mapN = texture(u_normalTex, uv).xyz * 2.0 - 1.0;
    mapN.xy *= u_normalScale;

    vec3 dp1 = dFdx(v_fragPos);
    vec3 dp2 = dFdy(v_fragPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 T = safeNormalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 B = safeNormalize(-dp1 * duv2.x + dp2 * duv1.x);
    mat3 TBN = mat3(T, B, N);
    return safeNormalize(TBN * mapN);
}

void main(){
    vec2 uv = v_uv * u_uvScale + u_uvOffset;
    vec4 baseTex = (u_useBaseColorTex != 0) ? texture(u_baseColorTex, uv) : vec4(1.0);
    vec4 baseColor = u_baseColor * baseTex * v_color;

    if(u_useAlphaClip != 0 && baseColor.a < u_alphaCutoff){
        discard;
    }

    float metallic = clamp(u_metallic, 0.0, 1.0);
    float roughness = clamp(u_roughness, 0.04, 1.0);
    if(u_useMetallicRoughnessTex != 0){
        vec4 mr = texture(u_metallicRoughnessTex, uv);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
    }

    float ao = 1.0;
    if(u_useOcclusionTex != 0){
        float occl = texture(u_occlusionTex, uv).r;
        ao = mix(1.0, occl, clamp(u_aoStrength, 0.0, 1.0));
    }

    vec3 N = getNormal(uv);

    float packedMode = metallic;
    if(u_surfaceMode == 1){
        packedMode = -0.5;
    }else if(u_surfaceMode == 2){
        packedMode = -1.0;
    }

    gAlbedo = vec4(baseColor.rgb, roughness);
    gNormal = vec4(safeNormalize(N), packedMode);
    gPosition = vec4(v_fragPos, ao);
}
