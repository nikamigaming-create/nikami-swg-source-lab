#version 450 core
layout(location = 0) out vec4 fColor;

layout(set = 2, binding = 0) uniform samplerCube sTexture;

layout(location = 0) in struct
{
	vec4 Color;
	vec2 UV;
	vec3 CubeDir;
} In;

void main()
{
	vec3 dir = In.CubeDir;
	if (dot(dir, dir) < 0.000001)
		dir = vec3(0.0, 0.0, 1.0);
	dir = normalize(dir);
	fColor = In.Color * texture(sTexture, dir);
}
