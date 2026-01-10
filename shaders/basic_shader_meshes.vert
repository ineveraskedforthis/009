#version 330 core

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;
// layout (location = 3) in vec3 instance;

out vec3 frag_normal;
out vec2 texcoord;
out vec3 position;

void main()
{
	gl_Position = projection * view * model * vec4(in_position, 1.0);
	frag_normal = normalize(mat3(model) * in_normal);
	texcoord = in_texcoord;
	position = (model * vec4(in_position, 1.0)).xyz;
}