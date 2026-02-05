#version 410 core

out vec4 FragColor;

float near = 0.1;
float far = 1.0;

float LinDepth(float depth){
    float z = depth * 2.0 - 1.0;

    return (2.0 * near * far) / (far + near - z * (far - near));
}

void main(){
    float depth = LinDepth(gl_FragCoord.z) / far;
    // Raise to a power less than 1.0 (like 0.5) to brighten dark areas
    float boostedDepth = pow(depth, 4); 
    FragColor = vec4(0.0,0.0,boostedDepth, 1.0);
}