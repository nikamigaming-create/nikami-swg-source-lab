#version 450 core
layout(location = 0) out vec4 fColor;

layout(set = 2, binding = 0) uniform sampler2D sTexture;
layout(set = 2, binding = 1) uniform sampler2D sTexture2;

layout(push_constant) uniform DrawPush
{
	vec4 auxParams;
	vec4 batchColor;
	vec4 textureFactor;
	vec4 textureFactor2;
	vec4 actorBaseColor;
	vec4 stageOps;
	vec4 stageArgs01;
	vec4 stageArgs23;
} Push;

layout(location = 0) in struct
{
	vec4 Color;
	vec3 Specular;
	vec2 UV;
	vec2 UV2;
} In;

vec4 stageTexture(int stageIndex)
{
	if (stageIndex == 1 && Push.auxParams.x > 1.5)
		return texture(sTexture2, In.UV2.st);
	return texture(sTexture, In.UV.st);
}

int decodePackedStageArg(float packedArgs, int argumentIndex)
{
	int packed = int(floor(packedArgs + 0.5));
	if (packed < 0)
		return -1;
	return (packed >> (argumentIndex * 6)) & 63;
}

vec4 stageArg(int packedArg, vec4 current, vec4 diffuse, vec4 specular, vec4 temp, vec4 texel)
{
	bool complement = packedArg >= 32;
	if (complement)
		packedArg -= 32;
	bool alphaReplicate = packedArg >= 16;
	if (alphaReplicate)
		packedArg -= 16;
	int arg = packedArg;
	vec4 value = current;
	if (arg == 0)
		value = current;
	else if (arg == 1)
		value = diffuse;
	else if (arg == 2)
		value = specular;
	else if (arg == 3)
		value = temp;
	else if (arg == 4)
		value = texel;
	else if (arg == 5)
		value = Push.textureFactor;
	if (alphaReplicate)
		value.rgb = vec3(value.a);
	if (complement)
		value = vec4(1.0) - value;
	return value;
}

vec4 applyStageOp(int op, vec4 arg0, vec4 arg1, vec4 arg2, vec4 textureValue, vec4 diffuseValue, vec4 currentValue)
{
	if (op == 1)
		return arg1;
	if (op == 2)
		return arg2;
	if (op == 3)
		return arg1 * arg2;
	if (op == 4)
		return arg1 * arg2 * 2.0;
	if (op == 5)
		return arg1 * arg2 * 4.0;
	if (op == 6)
		return arg1 + arg2;
	if (op == 7)
		return arg1 + arg2 - vec4(0.5);
	if (op == 8)
		return (arg1 + arg2 - vec4(0.5)) * 2.0;
	if (op == 9)
		return arg1 - arg2;
	if (op == 10)
		return arg1 + arg2 * (vec4(1.0) - arg1);
	if (op == 11)
		return mix(arg2, arg1, diffuseValue.a);
	if (op == 12)
		return mix(arg2, arg1, textureValue.a);
	if (op == 13)
		return mix(arg2, arg1, Push.textureFactor.a);
	if (op == 14)
		return arg1 + arg2 * (1.0 - textureValue.a);
	if (op == 15)
		return mix(arg2, arg1, currentValue.a);
	if (op == 16)
		return arg1;
	if (op == 17)
		return vec4(arg1.rgb + arg1.a * arg2.rgb, arg1.a);
	if (op == 18)
		return vec4(arg1.rgb * arg2.rgb + vec3(arg1.a), arg1.a);
	if (op == 19)
		return vec4((1.0 - arg1.a) * arg2.rgb + arg1.rgb, arg1.a);
	if (op == 20)
		return vec4((vec3(1.0) - arg1.rgb) * arg2.rgb + vec3(arg1.a), arg1.a);
	if (op == 21 || op == 22)
		return arg1;
	if (op == 23)
	{
		float dot3 = clamp(dot(arg1.rgb * 2.0 - 1.0, arg2.rgb * 2.0 - 1.0), 0.0, 1.0);
		return vec4(dot3);
	}
	if (op == 24)
		return arg0 + arg1 * arg2;
	if (op == 25)
		return mix(arg2, arg1, arg0);
	return arg1;
}

void applyStage(inout vec4 current, inout vec4 temp, int stageIndex)
{
	int colorOp = stageIndex == 0 ? int(floor(Push.stageOps.x + 0.5)) : int(floor(Push.stageOps.z + 0.5));
	int alphaOp = stageIndex == 0 ? int(floor(Push.stageOps.y + 0.5)) : int(floor(Push.stageOps.w + 0.5));
	if (colorOp <= 0 && alphaOp <= 0)
		return;

	vec4 texel = stageTexture(stageIndex);
	vec4 diffuse = In.Color;
	vec4 specular = vec4(In.Specular, 1.0);
	float colorArgsPacked = stageIndex == 0 ? Push.stageArgs01.x : Push.stageArgs23.x;
	float alphaArgsPacked = stageIndex == 0 ? Push.stageArgs01.y : Push.stageArgs23.y;
	int resultArg = stageIndex == 0 ? int(floor(Push.stageArgs01.z + 0.5)) : int(floor(Push.stageArgs23.z + 0.5));
	vec4 color0 = stageArg(decodePackedStageArg(colorArgsPacked, 0), current, diffuse, specular, temp, texel);
	vec4 color1 = stageArg(decodePackedStageArg(colorArgsPacked, 1), current, diffuse, specular, temp, texel);
	vec4 color2 = stageArg(decodePackedStageArg(colorArgsPacked, 2), current, diffuse, specular, temp, texel);
	vec4 alpha0 = stageArg(decodePackedStageArg(alphaArgsPacked, 0), current, diffuse, specular, temp, texel);
	vec4 alpha1 = stageArg(decodePackedStageArg(alphaArgsPacked, 1), current, diffuse, specular, temp, texel);
	vec4 alpha2 = stageArg(decodePackedStageArg(alphaArgsPacked, 2), current, diffuse, specular, temp, texel);
	vec4 colorResult = colorOp > 0 ? applyStageOp(colorOp, color0, color1, color2, texel, diffuse, current) : current;
	vec4 alphaResult = alphaOp > 0 ? applyStageOp(alphaOp, alpha0, alpha1, alpha2, texel, diffuse, current) : current;
	vec4 combined = clamp(vec4(colorResult.rgb, alphaResult.a), 0.0, 1.0);
	if (resultArg == 3)
		temp = combined;
	else
		current = combined;
}

void main()
{
	int pixelProgramMode = int(floor(Push.auxParams.y + 0.5));
	vec4 sampled = texture(sTexture, In.UV.st);
	vec4 sampled2 = Push.auxParams.x > 1.5 ? texture(sTexture2, In.UV2.st) : sampled;
	if (pixelProgramMode == 11)
	{
		vec2 radarUv = (gl_FragCoord.xy - Push.actorBaseColor.xy) / max(Push.actorBaseColor.zw, vec2(1.0));
		if (dot(radarUv, radarUv) > 1.0)
			discard;
	}
	vec4 color;
	if (Push.stageOps.x >= 0.0)
	{
		color = In.Color;
		vec4 temp = vec4(0.0);
		applyStage(color, temp, 0);
		if (Push.stageOps.z >= 0.0 || Push.stageOps.w >= 0.0)
			applyStage(color, temp, 1);
		else if (Push.auxParams.x > 1.5)
			color.a *= texture(sTexture2, In.UV2.st).a;
	}
	else
	{
		color = In.Color * sampled;
	}
	if (pixelProgramMode == 1 || pixelProgramMode == 21)
		color = sampled;
	else if (pixelProgramMode == 2)
		color = In.Color * Push.textureFactor;
	else if (pixelProgramMode == 3)
		color = sampled * Push.textureFactor;
	else if (pixelProgramMode == 4)
		color = mix(sampled2, sampled, In.Color.a);
	else if (pixelProgramMode == 5)
		color = vec4(0.2, 0.2, 0.2, sampled.a * sampled.a);
	else if (pixelProgramMode == 9)
		color = vec4(sampled.rgb * In.Color.rgb, sampled.a);
	else if (pixelProgramMode == 10)
		color = vec4((sampled.rgb * In.Color.rgb) + (In.Specular * sampled2.a), sampled.a);
	else if (pixelProgramMode == 11)
		color = vec4(sampled.rgb * Push.textureFactor.rgb, sampled2.a);
	else if (pixelProgramMode == 12)
		color = vec4(sampled.rgb * In.Color.rgb, sampled.a) * Push.textureFactor;
	else if (pixelProgramMode == 13 || pixelProgramMode == 22)
		color = vec4(sampled.rgb * In.Color.rgb * Push.textureFactor.rgb, 1.0);
	else if (pixelProgramMode == 14 || pixelProgramMode == 23)
	{
		vec3 hue = mix(Push.textureFactor.rgb, Push.textureFactor2.rgb, sampled.a);
		color = vec4(sampled.rgb * hue * In.Color.rgb, 1.0);
	}
	else if (pixelProgramMode == 15 || pixelProgramMode == 24)
	{
		vec3 specularRaw = In.Specular * sampled2.a;
		color = vec4((sampled.rgb * In.Color.rgb * Push.textureFactor.rgb) + specularRaw, sampled.a);
	}
	else if (pixelProgramMode == 16 || pixelProgramMode == 25)
	{
		vec3 hue = mix(Push.textureFactor.rgb, Push.textureFactor2.rgb, sampled.a);
		vec3 specularRaw = In.Specular * sampled2.a;
		color = vec4((sampled.rgb * hue * In.Color.rgb) + specularRaw, sampled.a);
	}
	else if (pixelProgramMode == 17)
		color = vec4(sampled.rgb * Push.textureFactor.rgb * In.Color.rgb, sampled.a);
	else if (pixelProgramMode == 18 || pixelProgramMode == 26)
	{
		float hueB = sampled2.a;
		float hueMask = clamp(sampled.a - hueB, 0.0, 1.0);
		vec3 hue = mix(Push.textureFactor2.rgb, Push.textureFactor.rgb, hueMask);
		color = vec4(sampled.rgb * hue * In.Color.rgb, clamp(sampled.a + hueB, 0.0, 1.0));
	}
	else if (pixelProgramMode == 19)
	{
		float hueB = sampled2.a;
		vec3 hueMain = mix(vec3(1.0), Push.textureFactor.rgb, sampled.a);
		vec3 hueBColor = mix(vec3(1.0), Push.textureFactor2.rgb, hueB);
		color = vec4(sampled.rgb * In.Color.rgb * hueMain * hueBColor, 1.0);
	}
	else if (pixelProgramMode == 20)
		color = vec4((sampled.rgb * In.Color.rgb * Push.textureFactor.rgb) + In.Specular, sampled.a);
	else if (pixelProgramMode == 27)
		color = sampled * In.Color;
	if (pixelProgramMode == 0)
		color.rgb = clamp(color.rgb + In.Specular, 0.0, 1.0);
	color.a *= Push.textureFactor.a;
	int compare = int(floor(Push.auxParams.z + 0.5));
	float reference = Push.auxParams.w;
	bool pass = true;
	if (compare >= 0)
	{
		if (compare == 0)
			pass = false;
		else if (compare == 1)
			pass = color.a < reference;
		else if (compare == 2)
			pass = abs(color.a - reference) < (0.5 / 255.0);
		else if (compare == 3)
			pass = color.a <= reference;
		else if (compare == 4)
			pass = color.a > reference;
		else if (compare == 5)
			pass = abs(color.a - reference) >= (0.5 / 255.0);
		else if (compare == 6)
			pass = color.a >= reference;
	}
	if (!pass)
		discard;
	fColor = color;
}
