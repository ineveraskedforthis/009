#version 330 core

uniform vec2 shift;
uniform float zoom;
uniform float aspect_ratio;

layout (location = 0) in vec3 in_position;
out vec3 pixel_position;

void main()
{
	vec3 result = in_position * zoom / vec3(aspect_ratio, 1.f, 1.f);
	result.xy += shift;
	gl_Position = vec4(result, 1.0);
	pixel_position = result;
}