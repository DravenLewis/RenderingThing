#version 330 core

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D screenTexture;
uniform vec2 outputTexelSize;

uniform float chromaStrength;
uniform float edgeStart;
uniform float edgeEnd;
uniform vec2 redShiftPx;
uniform vec2 blueShiftPx;

void main(){
    vec4 base = texture(screenTexture, TexCoords);

    vec2 centeredUv = (TexCoords * 2.0) - 1.0;
    float radius = length(centeredUv);
    float falloffEnd = max(edgeEnd, edgeStart + 0.0001);
    float falloff = smoothstep(edgeStart, falloffEnd, radius);

    vec2 redOffset = redShiftPx * outputTexelSize * chromaStrength * falloff;
    vec2 blueOffset = blueShiftPx * outputTexelSize * chromaStrength * falloff;

    float r = texture(screenTexture, clamp(TexCoords + redOffset, vec2(0.0), vec2(1.0))).r;
    float b = texture(screenTexture, clamp(TexCoords - blueOffset, vec2(0.0), vec2(1.0))).b;

    FragColor = vec4(r, base.g, b, base.a);
}
