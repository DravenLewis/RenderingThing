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
uniform sampler2D u_roughnessTex;
uniform int u_useRoughnessTex;
uniform sampler2D u_metallicRoughnessTex;
uniform int u_useMetallicRoughnessTex;

uniform sampler2D u_normalTex;
uniform int u_useNormalTex;
uniform float u_normalScale;
uniform sampler2D u_heightTex;
uniform int u_useHeightTex;
uniform float u_heightScale;

uniform sampler2D u_occlusionTex;
uniform int u_useOcclusionTex;
uniform float u_aoStrength;

uniform vec2 u_uvScale;
uniform vec2 u_uvOffset;
uniform vec3 u_viewPos;

uniform int u_useAlphaClip;
uniform float u_alphaCutoff;
uniform int u_surfaceMode; // 0=PBR, 1=LegacyLit, 2=LegacyUnlit

vec3 safeNormalize(vec3 v){
    float lenV = length(v);
    return (lenV > 1e-5) ? (v / lenV) : vec3(0.0, 1.0, 0.0);
}

mat3 buildTBN(vec2 uv, vec3 N){
    vec3 dp1 = dFdx(v_fragPos);
    vec3 dp2 = dFdy(v_fragPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 T = safeNormalize(dp1 * duv2.y - dp2 * duv1.y);
    vec3 B = safeNormalize(-dp1 * duv2.x + dp2 * duv1.x);
    return mat3(T, B, N);
}

vec2 applyHeightMapParallax(vec2 uv, vec2 duvDx, vec2 duvDy, vec3 viewDirWS, mat3 TBN){
    if(u_useHeightTex == 0 || u_heightScale <= 0.0){
        return uv;
    }
    vec3 viewDirTS = transpose(TBN) * safeNormalize(viewDirWS);
    float viewZ = max(viewDirTS.z, 0.0);
    if(viewZ <= 1e-4){
        return uv;
    }

    // Reduce parallax near grazing angles and UV borders to avoid seam lines/smearing.
    float angleFade = viewZ * viewZ;
    float edgeDist = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    float edgeFade = smoothstep(0.02, 0.08, edgeDist);
    vec2 hTexSize = vec2(textureSize(u_heightTex, 0));
    float texelFootprint = max(length(duvDx * hTexSize), length(duvDy * hTexSize));
    float minifyFade = 1.0 - smoothstep(1.0, 4.0, texelFootprint);
    float viewDist = length(u_viewPos - v_fragPos);
    float distanceFade = 1.0 - smoothstep(12.0, 45.0, viewDist);
    float parallaxFade = angleFade * edgeFade * minifyFade * distanceFade;
    if(parallaxFade <= 1e-4){
        return uv;
    }
    float parallaxStrength = u_heightScale * parallaxFade;

    float denom = max(viewZ, 0.25);
    float height = textureGrad(u_heightTex, uv, duvDx, duvDy).r;
    float parallax = (height - 0.5) * parallaxStrength;
    vec2 displacedUv = uv - (viewDirTS.xy / denom) * parallax;

    // Keep sampling in-range for clamp-to-edge textures.
    return clamp(displacedUv, vec2(0.001), vec2(0.999));
}

vec3 getNormal(vec2 uv, vec2 duvDx, vec2 duvDy, vec3 N, mat3 TBN){
    if(u_useNormalTex == 0){
        return N;
    }

    vec3 mapN = textureGrad(u_normalTex, uv, duvDx, duvDy).xyz * 2.0 - 1.0;
    mapN.xy *= u_normalScale;
    return safeNormalize(TBN * mapN);
}

void main(){
    vec2 uvBase = v_uv * u_uvScale + u_uvOffset;
    vec2 duvDx = dFdx(uvBase);
    vec2 duvDy = dFdy(uvBase);
    vec3 baseN = safeNormalize(v_normal);
    mat3 TBN = buildTBN(uvBase, baseN);
    vec3 V = safeNormalize(u_viewPos - v_fragPos);
    vec2 uv = applyHeightMapParallax(uvBase, duvDx, duvDy, V, TBN);
    vec4 baseTex = (u_useBaseColorTex != 0) ? textureGrad(u_baseColorTex, uv, duvDx, duvDy) : vec4(1.0);
    vec4 baseColor = u_baseColor * baseTex * v_color;

    if(u_useAlphaClip != 0 && baseColor.a < u_alphaCutoff){
        discard;
    }

    float metallic = clamp(u_metallic, 0.0, 1.0);
    float roughness = clamp(u_roughness, 0.04, 1.0);
    if(u_useRoughnessTex != 0){
        float roughSample = textureGrad(u_roughnessTex, uv, duvDx, duvDy).r;
        roughness = clamp(roughness * roughSample, 0.04, 1.0);
    }
    if(u_useMetallicRoughnessTex != 0){
        vec4 mr = textureGrad(u_metallicRoughnessTex, uv, duvDx, duvDy);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
    }

    float ao = 1.0;
    if(u_useOcclusionTex != 0){
        float occl = textureGrad(u_occlusionTex, uv, duvDx, duvDy).r;
        ao = mix(1.0, occl, clamp(u_aoStrength, 0.0, 1.0));
    }

    vec3 N = getNormal(uv, duvDx, duvDy, baseN, TBN);

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
