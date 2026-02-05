#version 410 core

layout (location = 0) in vec3 aPos;

out vec3 v_dir;

uniform mat4 u_view;
uniform mat4 u_projection;

void main() {
    // Remove camera translation so the skybox stays centered
    mat4 viewNoTrans = mat4(mat3(u_view));
    vec3 dir = normalize(aPos);
    vec4 pos = u_projection * viewNoTrans * vec4(dir, 1.0);
    gl_Position = pos.xyww;
    v_dir = dir;
}
