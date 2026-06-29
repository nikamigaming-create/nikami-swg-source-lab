#version 450 core
layout(location = 0) in vec4 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec3 aCubeDir;

layout(set = 1, binding = 0) uniform UBO
{
	vec2 uScale;
	vec2 uTranslate;
} ubo;

layout(location = 0) out struct
{
	vec4 Color;
	vec2 UV;
	vec3 CubeDir;
} Out;

void main()
{
	Out.Color = aColor;
	Out.CubeDir = aCubeDir;
	vec2 ndc = aPos.xy * ubo.uScale + ubo.uTranslate;
	Out.UV = ndc * vec2(0.5, -0.5) + vec2(0.5, 0.5);
	float w = (abs(aPos.w) > 0.00001) ? aPos.w : 1.0;
	gl_Position = vec4(ndc * w, aPos.z * w, w);
	gl_Position.y *= -1.0;
}
