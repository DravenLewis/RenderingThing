#version 410 core

in vec3 v_dir;
out vec4 FragColor;

uniform samplerCube u_envMap;

void main() {
    vec3 col = texture(u_envMap, normalize(v_dir)).rgb;
    FragColor = vec4(col, 1.0);
}
