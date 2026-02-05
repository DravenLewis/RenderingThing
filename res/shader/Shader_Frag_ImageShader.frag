#version 410 core

in vec2 v_uv;

out vec4 FragColor;

uniform sampler2D u_texture;
uniform vec4 u_color;
uniform vec2 u_uv;

void main(){
    vec2 uv = v_uv + u_uv;
    vec4 texColor = texture(u_texture,uv);
    FragColor = texColor * u_color;
}