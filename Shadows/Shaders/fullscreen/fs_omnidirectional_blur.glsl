#version 330 core

#include "common.glsl"

precision mediump float;
precision mediump sampler2DArray;

uniform sampler2D tex_object;
uniform vec2 PixelSize;
uniform float IsVertical;
uniform float MaxDist;

#define FILTER_STEP_COUNT 20.0
#define FILTER_SIZE vec2(FILTER_STEP_COUNT, FILTER_STEP_COUNT)
#define COUNT (FILTER_STEP_COUNT * 2.0 + 1.0)

in vec2 TexCoord_;

out vec4 color;

void main()
{
    vec2 radiusUV = (FILTER_SIZE * PixelSize) / FILTER_STEP_COUNT;

    if (IsVertical > 0.0)
    {
        radiusUV = vec2(0.0, radiusUV.y);
    }
    else
    {
        radiusUV = vec2(radiusUV.x, 0.0);
    }

	float inv6 = 1.0 / 6.0;

    vec4 colorTemp = vec4(0);
    for (float x = -FILTER_STEP_COUNT; x <= FILTER_STEP_COUNT; ++x)
	{
        vec2 offset = vec2(x, x / 6.0) * radiusUV;
        vec2 tex = TexCoord_ + offset;

		//colorTemp = vec4(0.0, 0.0, 0.0, COUNT);

		int index = int(tex.y / inv6);

		TexArrayUV uv;
		uv.u = tex.x;
		uv.v = (tex.y - (inv6 * index)) * 6.0;
		uv.index = index;
		tex = Convert_TexArrayUV_To_Tex2dUV(uv);

		index = int(tex.y / inv6);
		uv.u = tex.x;
		uv.v = (tex.y - (inv6 * index)) * 6.0;
		uv.index = index;
		tex = Convert_TexArrayUV_To_Tex2dUV(uv);

        if (tex.x < 0.0 || tex.x > 1.0 || tex.y < 0.0 || tex.y > 1.0)
        {
            colorTemp.x += exp(MaxDist);
            colorTemp.y += exp(MaxDist * MaxDist);
            colorTemp.w += 1.0;
        }
        else
        {
            colorTemp += texture(tex_object, tex);
        }
    }

    color = colorTemp / COUNT;
}