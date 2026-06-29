#version 450 core
layout(location = 0) in vec4 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec3 aSpecular;
layout(location = 4) in vec2 aUV2;

layout(set = 1, binding = 0) uniform UBO
{
	vec2 uScale;
	vec2 uTranslate;
} ubo;

layout(location = 0) out struct
{
	vec4 Color;
	vec3 Specular;
	vec2 UV;
	vec2 UV2;
} Out;

void main()
{
	Out.Color = aColor;
	Out.Specular = aSpecular;
	Out.UV = aUV;
	Out.UV2 = aUV2;
	float w = (abs(aPos.w) > 0.00001) ? aPos.w : 1.0;
	vec2 ndc = aPos.xy * ubo.uScale + ubo.uTranslate;
	gl_Position = vec4(ndc * w, aPos.z * w, w);
	gl_Position.y *= -1.0;
}
