#version 330 core

// shadow things
uniform sampler2DArray shadow_map;
uniform mat4 shadow_transform [10];
uniform int shadow_layers;

// color and lighting
uniform vec3 albedo;
uniform vec3 ambient;
uniform vec3 light_direction;
uniform vec3 light_color;
uniform vec3 camera_position;

layout (location = 0) out vec4 out_color;

// position things
in vec3 frag_normal;
in vec2 texcoord;
in vec3 position;

vec3 specular(vec3 albedo, vec3 direction) {
	float cosine = dot(frag_normal, direction);
	float light_factor = max(0.0, cosine);
	vec3 reflected_direction = 2.0 * frag_normal * cosine - direction;
	vec3 view_direction = normalize(camera_position - position);

	return 0.5 * albedo
			* pow(
				max(
					0.0,
					dot(
						reflected_direction,
						view_direction
					)
				),
				1 / 0.5 / 0.5 - 1
			);
}

float TonemapRaw(float x)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float Uncharted2Tonemap(float luma)
{
	float W = 11.2;
	return TonemapRaw(luma) / TonemapRaw(W);
}

void main()
{
	// vec4 texture_value = texture(albedo, texcoord);
	// vec4 texture_value = vec4(0.9f, 0.9f, 0.9f, 1.f);
	vec3 albedo_color = albedo;

	// if (texture_value.a <= 0.5) {
	//     discard;
	// }


	float shadow_factor = 1.0;

	bool in_shadow_texture = false;
	int current_shadow_layer = 0;
	vec4 shadow_pos;

	float cos_angle = dot(normalize(light_direction), frag_normal);
	float tan_angle = abs(tan(acos(cos_angle)));

	for (int i = 0; i < shadow_layers; i++) {
		shadow_pos = shadow_transform[i] * vec4(position, 1.0);
		shadow_pos /= shadow_pos.w;
		shadow_pos = shadow_pos * 0.5 + vec4(0.5);
		in_shadow_texture =
			(shadow_pos.x > 0.0)
			&& (shadow_pos.x < 1.0)
			&& (shadow_pos.y > 0.0)
			&& (shadow_pos.y < 1.0)
			&& (shadow_pos.z > 0.0)
			&& (shadow_pos.z < 1.0);


		if (in_shadow_texture) {
			current_shadow_layer = i;
			break;
		}
	}

	if (in_shadow_texture)
	{
		vec2 sum = vec2(0.0, 0.0);
		float sum_w = 0.0;
		const int N = 4;
		float radius = 1.0;
		for (int x = -N; x <= N; ++x)
		{
			for (int y = -N; y <= N; ++y)
			{
				float c = exp(-float(x*x + y*y) / (radius*radius));
				vec3 texcoord = vec3(shadow_pos.xy + vec2(float(x), float(y)) / 1024.0 / 2.0, current_shadow_layer);
				sum += c * texture(shadow_map, texcoord).rg;
				sum_w += c;
			}
		}

		vec2 data = sum / sum_w;

		// vec2 data = texture(shadow_map, shadow_pos.xy).rg;
		float actual_length_of_light_ray = data.r;
		float sigma = data.g - actual_length_of_light_ray * actual_length_of_light_ray;
		float potential_length_of_light_ray = shadow_pos.z - 0.001;// ;

		float length_of_shadow_ray = potential_length_of_light_ray - actual_length_of_light_ray;

		float cheba = sigma / (sigma + length_of_shadow_ray * length_of_shadow_ray);
		// float delta = 0.150;
		float delta = 0.05;
		cheba = (cheba - delta) / (1.f - delta);
		if (cheba < 0.0) cheba = 0.0;

		shadow_factor = (length_of_shadow_ray < 0) ? 1.0 : cheba;

		// out_color = vec4(length_of_shadow_ray * 1000.f, 0, 0, 1);
		// return;

		// if (length_of_shadow_ray > 0) {
		//     out_color = vec4(cheba, 0, 0, 1);
		//     return;
		// }
	}


	float diffuse = max(0.0, dot(normalize(frag_normal), light_direction));


	vec3 light = ambient;

	light += light_color * diffuse * shadow_factor;

	vec3 color = albedo_color * light + specular(albedo_color, light_direction) * shadow_factor;


	vec3 step_to_camera = (camera_position - position) / 10;
	vec3 current_position = position + step_to_camera * 0.5;
	float step_length = length(step_to_camera);
	float absorbtion = 0.01;
	vec3 emission = ambient * absorbtion * 1.5;
	float absorbtion_exp = exp(-absorbtion * step_length);

	for (int i = 0; i < 10 - 1; i++) {
		color = absorbtion_exp * color + (1 - absorbtion_exp) * emission / absorbtion;
		if (!in_shadow_texture) {
			color = color + light_color * exp(-absorbtion * shadow_pos.z) * 0.0003;
		}
		current_position = current_position + step_to_camera;
	}


	float Kr = 0.299;
	float Kg = 0.587;
	float Kb = 0.114;

	mat3 toYPbPr = mat3(
		Kr,     -0.5 * Kr / (1 - Kb),    0.5,
		Kg,     -0.5 * Kg / (1 - Kb),   -0.5 * Kg / (1 - Kr),
		Kb,      0.5,                   -0.5 * Kb / (1 - Kr)
	);

	mat3 fromYPbPr = inverse(toYPbPr);

	vec3 colorYPbPr = toYPbPr * color;

	colorYPbPr.x = Uncharted2Tonemap(colorYPbPr.x);

	color = fromYPbPr * colorYPbPr;

	color = pow(color, vec3(1.0 / 2.2));


	out_color = vec4(color, 1.0);
}