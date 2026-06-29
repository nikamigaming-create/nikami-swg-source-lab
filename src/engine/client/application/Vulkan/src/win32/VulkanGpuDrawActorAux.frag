#version 450 core
layout(location = 0) out vec4 fColor;

layout(set = 2, binding = 0) uniform sampler2D sNormalTexture;
layout(set = 2, binding = 1) uniform sampler2D sBaseTexture;

layout(push_constant) uniform ActorAuxPush
{
	vec4 auxParams;
	vec4 batchColor;
	vec4 textureFactor;
	vec4 textureFactor2;
	vec4 actorBaseColor;
} Push;

layout(location = 0) in struct
{
	vec4 Color;
	vec2 UV;
} In;

void main()
{
	vec4 normalSample = texture(sNormalTexture, In.UV.st);
	vec4 baseSample = texture(sBaseTexture, In.UV.st);
	vec3 normal = normalize(normalSample.rgb * 2.0 - 1.0);
	vec3 lightDir = normalize(vec3(-0.35, -0.45, 0.82));
	float hemi = clamp(dot(normal, lightDir) * 0.5 + 0.5, 0.30, 1.0);
	float rim = pow(clamp(1.0 - abs(normal.z), 0.0, 1.0), 2.0);
	vec3 fallbackColor = clamp(max(Push.batchColor.rgb, In.Color.rgb * 0.72), 0.0, 1.0);
	vec3 sampledBaseColor = clamp(baseSample.rgb * max(In.Color.rgb, vec3(0.18)), 0.0, 1.0);
	vec3 linkedBaseColor = mix(fallbackColor, Push.actorBaseColor.rgb, clamp(Push.actorBaseColor.a, 0.0, 1.0));
	vec3 materialColor = mix(linkedBaseColor, sampledBaseColor, 0.78);
	vec3 tfColor = clamp(max(Push.textureFactor.rgb, vec3(0.18)), 0.0, 1.0);
	vec3 metalBase = clamp(mix(materialColor, materialColor * tfColor, 0.10), 0.0, 1.0);
	vec3 lit = metalBase * (0.92 + 0.14 * hemi) + materialColor * rim * 0.035;
	float auxAlpha = clamp(In.Color.a * Push.auxParams.x, 0.0, 1.0);
	fColor = vec4(clamp(lit, 0.0, 1.0), auxAlpha);
}
