#version 330 core
#preprocessor

precision mediump float;

#define EDGE_THRESHOLD_MIN 0.0312
#define EDGE_THRESHOLD_MAX 0.125
#define ITERATIONS 5
#define SUBPIXEL_QUALITY 0.75

uniform sampler2D InputSampler;

uniform vec2 PixelSize;

in vec2 TexCoord_;
out vec4 color;

float rgb2luma(vec3 rgb) 
{
	return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

float QUALITY(int i)
{
	const float steps[7] = float[7](1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);
	return steps[i];
}

void FindPixelOffset(out float finalOffset, float stepLength, float lumaLocalAverage, float gradientScaled, float lumaCenter, bool isHorizontal)
{
	// UV �� �ùٸ� �������� ���ȼ� �̵���Ŵ
	vec2 currentUV = TexCoord_;
	if (isHorizontal)
		currentUV.y += stepLength * 0.5;
	else
		currentUV.x += stepLength * 0.5;

	// ������ �������� offset (�� �ݺ� ���ǿ� ����) �� ���.
	vec2 offset = isHorizontal ? vec2(PixelSize.x, 0.0) : vec2(0.0, PixelSize.y);

	// �����ڸ��� �� ������ �����Ͽ� Ž���ϱ� ���ؼ� UV�� ���. QUALITY �� ������ �� ������ ��.
	vec2 uv1 = currentUV - offset;
	vec2 uv2 = currentUV + offset;

	// Ž���ϴ� ���׸�Ʈ�� ���� ������ lumas�� �а�, delta �� ����ϰ� ���� ��� luma�� ���
	float lumaEnd1 = rgb2luma(texture(InputSampler, uv1).rgb);
	float lumaEnd2 = rgb2luma(texture(InputSampler, uv2).rgb);
	lumaEnd1 -= lumaLocalAverage;
	lumaEnd2 -= lumaLocalAverage;

	// ���� �������� luma delta�� ���� ��ȭ�� ���� ũ��, �츮�� �����ڸ��� ���鿡 ������ ����
	bool reached1 = abs(lumaEnd1) >= gradientScaled;
	bool reached2 = abs(lumaEnd2) >= gradientScaled;
	bool reachedBoth = reached1 && reached2;

	// ���鿡 �������� ���ߴٸ�, �츮�� ����ؼ� �� �������� Ž����.
	if (!reached1)
		uv1 -= offset;

	if (!reached2)
		uv2 += offset;

	// ���� �� ���� ��� ���鿡 �������� �ʾҴٸ�, ����ؼ� Ž��
	if (!reachedBoth)
	{
		for (int i = 2; i < ITERATIONS; i++)
		{
			// �ʿ��ϴٸ�, ù���� ������ luma�� ����, delta ���
			if (!reached1)
			{
				lumaEnd1 = rgb2luma(texture(InputSampler, uv1).rgb);
				lumaEnd1 = lumaEnd1 - lumaLocalAverage;
			}

			// �ʿ��ϴٸ�, �ݴ� ������ luma�� ����, delta ���
			if (!reached2)
			{
				lumaEnd2 = rgb2luma(texture(InputSampler, uv2).rgb);
				lumaEnd2 = lumaEnd2 - lumaLocalAverage;
			}

			// ���� ���� ������ luma delta�� ���� ��ȭ�� ���� ũ�ٸ�, �츮�� �����ڸ� ���鿡 ������ ����
			reached1 = abs(lumaEnd1) >= gradientScaled;
			reached2 = abs(lumaEnd2) >= gradientScaled;
			reachedBoth = reached1 && reached2;

			// ���� ���鿡 �������� �ʾҴٸ�, �츮�� �� �������� ��� Ž��, 
			// ���� ǰ���� ���� (���� : ������ �ݺ��� ���� �����ϰڴٴ� �ǹ�)
			if (!reached1)
				uv1 -= offset * QUALITY(i);

			if (!reached2)
				uv2 += offset * QUALITY(i);

			// �� ���鿡 �����ߴٸ�, Ž���� �ߴ�
			if (reachedBoth) { break; }
		}
	}

	// �� �� �����ڸ������� �Ÿ��� ���
	float distance1 = isHorizontal ? (TexCoord_.x - uv1.x) : (TexCoord_.y - uv1.y);
	float distance2 = isHorizontal ? (uv2.x - TexCoord_.x) : (uv2.y - TexCoord_.y);

	// � ������ �����ڸ��� ���� �� ������?
	bool isDirection1 = distance1 < distance2;
	float distanceFinal = min(distance1, distance2);

	// �����ڸ��� ����
	float edgeThickness = (distance1 + distance2);

	// UV offset: �����ڸ��� ������� ���� ����� �������� ����
	float pixelOffset = -distanceFinal / edgeThickness + 0.5;

	// �߾� luma�� �κ� ��պ��� �� �۳�?
	bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;

	// ���� �߾� luma�� �װ��� �̿����� �� �۴ٸ�, �� ���� delta luma�� ������� �մϴ�. (���� ����)
	// (�����ڸ��� ����� �� ����� ��������)
	bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;

	// ���� luma ���̰� �߸��� ���, offset �� �������� ����
	finalOffset = correctVariation ? pixelOffset : 0.0;
}

void SubPixelShifting(out float finalOffset, float lumaCenter, float lumaRange, float lumaDownUp, float lumaLeftRight, float lumaLeftCorners, float lumaRightCorners)
{
	// 3x3 �̿��� ���� luma�� ��ü ���� ���
	float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);

	// 3x3 �̿��� �縶 ������ ����, ���� ��հ� �߾� luma ���� delta�� ����
	float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);

	// �� delta�� ����� sub-pixel�� offset ���, �Ʒ� 2���� ���� [0~SUBPIXEL_QUALITY] ���� ����·� �����ϴ� ��
	float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
	float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * SUBPIXEL_QUALITY;

	// �� offset �� �� ū���� ��
	finalOffset = max(finalOffset, subPixelOffsetFinal);
}

void main()
{
	vec3 colorCenter = texture(InputSampler, TexCoord_).rgb;

	// ���� �ȼ��� luma
	float lumaCenter = rgb2luma(colorCenter);

	// ���� �ȼ� �߽����� 4���⿡ ���� luma�� ����
	// Luma at the four direct neighbours of the current fragment.
	float lumaDown = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(0, -1)).rgb);
	float lumaUp = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(0, 1)).rgb);
	float lumaLeft = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(-1, 0)).rgb);
	float lumaRight = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(1, 0)).rgb);

	// ���� �ȼ��� �� �ֺ� �ȼ��� luma �� ���� ū ���� ����.
	float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
	float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));

	// delta ���
	float lumaRange = lumaMax - lumaMin;

	// ���� luma ���̰� �Ӱ谪���� �۴ٸ�(�Ǵ� �츮�� ���� ��ο� �������� �ִٸ�), 
	// �츮�� �����ڸ��� ���� �ʴ� ���̹Ƿ�, AA�� �������� �ʴ´�.
	if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
		color = vec4(colorCenter, 1.0);
		return;
	}

	// ���� 4���� �ڳ� lumas �� ����
	float lumaDownLeft = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(-1, -1)).rgb);
	float lumaUpRight = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(1, 1)).rgb);
	float lumaUpLeft = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(-1, 1)).rgb);
	float lumaDownRight = rgb2luma(textureOffset(InputSampler, TexCoord_, ivec2(1, -1)).rgb);

	// 4 ���� �����ڸ��� lumas�� ����(���� ������ ���� ����� ���� �߰� ������ ���)
	float lumaDownUp = lumaDown + lumaUp;
	float lumaLeftRight = lumaLeft + lumaRight;

	// �ڳʵ� ����
	float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
	float lumaDownCorners = lumaDownLeft + lumaDownRight;
	float lumaRightCorners = lumaDownRight + lumaUpRight;
	float lumaUpCorners = lumaUpRight + lumaUpLeft;

	// ����� ���� ���� ������ ��ȭ���� ����ġ ���
	float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 + abs(-2.0 * lumaRight + lumaRightCorners);
	float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 + abs(-2.0 * lumaDown + lumaDownCorners);

	// ���� �����ڸ��� �����ΰ�? �����ΰ�?
	bool isHorizontal = (edgeHorizontal >= edgeVertical);

	// ���� �����ڸ��� �ݴ�������� 2���� �̿� �ؼ��� ������
	float luma1 = isHorizontal ? lumaDown : lumaLeft;
	float luma2 = isHorizontal ? lumaUp : lumaRight;

	// �� �������� ��ȭ���� ���
	float gradient1 = luma1 - lumaCenter;
	float gradient2 = luma2 - lumaCenter;

	// ��� ������ ��ȭ�� �� ���ĸ���?
	bool is1Steepest = abs(gradient1) >= abs(gradient2);

	// �ش� ������ ��ȭ��, ����ȭ
	float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

	// �����ڸ��� ���⿡ ���� ���� ũ��(�� �ȼ�)�� ����
	float stepLength = isHorizontal ? PixelSize.y : PixelSize.x;

	// �ùٸ� ������ ��� luma
	float lumaLocalAverage = 0.0;

	if (is1Steepest)
	{
		// ������ �ٲ�
		stepLength = -stepLength;
		lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
	}
	else
	{
		lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
	}

	float finalOffset = 0.0;
	FindPixelOffset(finalOffset, stepLength, lumaLocalAverage, gradientScaled, lumaCenter, isHorizontal);

	// Sub-pixel shifting
	SubPixelShifting(finalOffset, lumaCenter, lumaRange, lumaDownUp, lumaLeftRight, lumaLeftCorners, lumaRightCorners);

	// ���� UV ��ǥ ���
	vec2 finalUv = TexCoord_;
	if (isHorizontal)
		finalUv.y += finalOffset * stepLength;
	else
		finalUv.x += finalOffset * stepLength;

	// ���ο� UV ��ǥ���� �÷��� �а� �����
	vec3 finalColor = texture(InputSampler, finalUv).rgb;
	color = vec4(finalColor, 1.0);
}