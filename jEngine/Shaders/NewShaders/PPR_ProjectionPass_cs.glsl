#version 430 core

layout(local_size_x = 1, local_size_y = 1) in;

layout(binding = 0, rgba32f) readonly uniform image2D WorldPosImage;
layout(binding = 1, r32ui) uniform uimage2D ImmediateBufferImage;

precision mediump float;

uniform vec2 ScreenSize;
uniform mat4 WorldToScreen;
uniform vec4 Plane;

// 'intermediate buffer'�� ���ڵ��ϱ� ���� ���
// ���� 2��Ʈ�� ��ǥ�� �ε����� ���� �Ҵ�
#define PPR_CLEAR_VALUE (0xffffffff)
#define PPR_PREDICTED_DIST_MULTIPLIER (8)
#define PPR_PREDICTED_OFFSET_RANGE_X (2048)
#define PPR_PREDICTED_DIST_OFFSET_X (PPR_PREDICTED_OFFSET_RANGE_X)
#define PPR_PREDICTED_DIST_OFFSET_Y (0)

// offset�� ������� ��ǥ�� �ε��� ���
// -> �� ������ y �� �ǵ��� �����ϸ� �� 4���� ��ǥ�谡 ����.
uint PPR_GetPackingBasisIndexTwoBits(const vec2 offset)
{
	if (abs(offset.x) >= abs(offset.y))
		return uint(offset.x >= 0 ? 0 : 1);
	return uint(offset.y >= 0 ? 2 : 3);
}

// ���ڵ��� ��ǥ�� �ε����� ������� ��ǥ�踦 ���ڵ���
mat2 PPR_DecodePackingBasisIndexTwoBits(uint packingBasisIndex)
{
	vec2 basis = vec2(0.0);
	packingBasisIndex &= 3;
	basis.x += uint(0) == packingBasisIndex ? 1 : 0;
	basis.x += uint(1) == packingBasisIndex ? -1 : 0;
	basis.y += uint(2) == packingBasisIndex ? 1 : 0;
	basis.y += uint(3) == packingBasisIndex ? -1 : 0;
	return mat2(vec2(basis.y, -basis.x), basis.xy);
}

// Offset�� ������ �Ҽ��� ��ŷ(Packing)��.
uint PPR_PackValue(const float _whole, float _fract, const bool isY)
{
	uint result = uint(0);
	// ���� �κ� ��ŷ
	result += uint(_whole + (isY ? PPR_PREDICTED_DIST_OFFSET_Y : PPR_PREDICTED_DIST_OFFSET_X));
	result *= PPR_PREDICTED_DIST_MULTIPLIER;
	
	// �Ҽ� �κ� ��ŷ
	_fract *= PPR_PREDICTED_DIST_MULTIPLIER;
	result += uint(min(floor(_fract + 0.5), PPR_PREDICTED_DIST_MULTIPLIER - 1));
	return result;
}

// Encode offset for 'intermediate buffer' storage
// 'intermediate buffer' ����Ҹ� ���� Offset�� ���ڵ� ��
uint PPR_EncodeIntermediateBufferValue(const vec2 offset)
{
	// Offset�� y���� �� �� ��ǥ���� �ε��� ����
	uint packingBasisIndex = PPR_GetPackingBasisIndexTwoBits(offset);
	mat2 packingBasisSnappedMatrix = PPR_DecodePackingBasisIndexTwoBits(packingBasisIndex);
	
	// Offset�� �����ο� �Ҽ��η� �и���
	vec2 _whole = floor(offset + 0.5);
	vec2 _fract = offset - _whole;

	// ���͸��� ����� �����̴� ���� 'swimming'�� ����� ���� ���ϱ� ���ؼ� �Ҽ� �κ��� �̷��� ��.
	vec2 dir = normalize(offset);
	_fract -= 2 * dir * dot(dir, _fract);

	// �� �κ��� ��ȯ�Ͽ� ��ǥ�� ����(basis)�� ����
	_whole = packingBasisSnappedMatrix * _whole;
	_fract = packingBasisSnappedMatrix * _fract;
	
	// �Ҽ��θ� 0..1 ������ ��
	_fract *= 0.707;
	_fract += 0.5;

	// ��� ���ڵ�
	uint result = uint(0);
	result += PPR_PackValue(_whole.y, _fract.y, true);
	result *= 2 * PPR_PREDICTED_OFFSET_RANGE_X * PPR_PREDICTED_DIST_MULTIPLIER;
	result += PPR_PackValue(_whole.x, _fract.x, false);
	result *= 4;
	result += packingBasisIndex;
	return result;
}

// 'intermediate buffer'�� �������ǵ� ����� ����մϴ�.
// 'originalPixelVpos' �� ���� 'mirroredWorldPos'�� �ȼ��� �������� ��Ŵ.
// Shape�� �־��� ��ġ�� �������ǵ� �ȼ��� �ٸ� Shape�� ���� �������� �ʴ� ���� ������ �ڿ� 'projection pass'���� �� �Լ��� ȣ��˴ϴ�.
void PPR_ProjectionPassWrite(ivec2 originalPixelVpos, vec3 mirroredWorldPos)
{
	vec4 projPosOrig = WorldToScreen * vec4(mirroredWorldPos, 1);
	vec4 projPos = projPosOrig / projPosOrig.w;

	bool AllComponentLessThanOne = (abs(projPos.x) < 1.0) && (abs(projPos.y) < 1.0);
	if (AllComponentLessThanOne)
	{
		vec2 targetCrd = (projPos.xy * vec2(0.5, 0.5) + 0.5) * ScreenSize;
		vec2 offset = targetCrd - (originalPixelVpos + 0.5);
		uint originalValue = uint(0);
		uint valueToWrite = PPR_EncodeIntermediateBufferValue(offset);
		imageAtomicMin(ImmediateBufferImage, ivec2(int(targetCrd.x), int(targetCrd.y)), valueToWrite);
	}
}

void main()
{
	ivec2 VPos = ivec2(gl_GlobalInvocationID.xy);		// Position in screen space
	vec3 WorldPos = imageLoad(WorldPosImage, VPos).xyz;

	float dist = dot(Plane.xyz, WorldPos) - Plane.w;

	// Upper-side of the plane is skip
	if (dist < 0.0)
		return;

	vec3 MirroredWorldPos = WorldPos + 2.0 * dist * (-Plane.xyz);
	PPR_ProjectionPassWrite(VPos, MirroredWorldPos);
}
