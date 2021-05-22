cbuffer MatrixBuffer
{
	matrix WorldMatrix;
	matrix ViewMatrix;
	matrix ProjectionMatrix;
};

struct VertexInputType
{
	float3 Position : POSITION;
	float4 Color : COLOR;
};

struct PixelInputType
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
};

PixelInputType ColorVertexShader(VertexInputType Input)
{
	PixelInputType Output;

	float4 Pos = float4(Input.Position, 1.0f);

	// ����, �� �׸��� ���� ��Ŀ� ���� ������ ��ġ�� ����մϴ�.
	Output.Position = mul(Pos, WorldMatrix);
	Output.Position = mul(Output.Position, ViewMatrix);
	Output.Position = mul(Output.Position, ProjectionMatrix);

	// �ȼ� ���̴��� ����� �Է� ����
	Output.Color = Input.Color;

	return Output;
}
