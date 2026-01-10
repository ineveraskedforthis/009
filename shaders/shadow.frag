#version 330 core

out vec4 out_color;

void main()
{
	float z = gl_FragCoord.z;
	float z_x = dFdx(z);
	float z_y = dFdy(z);
	float slope = 0.25 * (z_x * z_x + z_y * z_y);
	out_color = vec4(z, z * z + slope, 0.0, 0.0);
}