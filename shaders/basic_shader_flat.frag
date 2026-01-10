#version 330 core

#define PI 3.1415926538
in vec3 pixel_position;
layout (location = 0) out vec4 out_color;

void main()
{
	out_color = vec4(pixel_position, 1.f);
}