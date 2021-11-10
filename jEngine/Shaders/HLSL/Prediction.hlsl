
cbuffer SceneConstantBuffer : register(b0)
{
	float4 offset;
	float4x4 MVP;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 color : COLOR;
};

PSInput VSMain(float3 position : POSITION, float3 color : COLOR)
{
	PSInput result;

	result.position = mul(MVP, float4(position + offset, 1.0f));
	result.color = color;

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.color, 0.5f);
}