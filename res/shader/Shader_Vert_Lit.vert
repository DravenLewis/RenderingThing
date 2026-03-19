#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aTexCoord;
layout (location = 4) in vec4 aTangent;
        
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_fragPos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;
out vec4 v_tangent;
    
void main() {
    mat3 normalMatrix = mat3(transpose(inverse(u_model)));
    v_fragPos = vec3(u_model * vec4(aPos, 1.0));
    v_normal = normalMatrix * aNormal;
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
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
}
