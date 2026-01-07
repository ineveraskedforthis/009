#version 330 core

layout (location = 0) in vec3 in_position;
out vec3 pixel_position;

void main()
{
    gl_Position = vec4(in_position, 1.0);
    pixel_position = in_position;
}