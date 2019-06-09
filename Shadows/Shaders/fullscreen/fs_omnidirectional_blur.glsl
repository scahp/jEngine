#version 330 core

#include "common.glsl"

precision mediump float;
precision mediump sampler2DArray;

uniform sampler2DArray tex_object_array;
uniform vec2 PixelSize;
uniform float IsVertical;
uniform float MaxDist;

#define FILTER_STEP_COUNT 20.0
#define FILTER_SIZE vec2(FILTER_STEP_COUNT, FILTER_STEP_COUNT)
#define COUNT (FILTER_STEP_COUNT * 2.0 + 1.0)

in vec2 TexCoord_;

layout(location = 0) out vec4 color0;
layout(location = 1) out vec4 color1;
layout(location = 2) out vec4 color2;
layout(location = 3) out vec4 color3;
layout(location = 4) out vec4 color4;
layout(location = 5) out vec4 color5;

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

    for(int k=0;k<6;++k)
    {
        vec4 color = vec4(0);
        for (float x = -FILTER_STEP_COUNT; x <= FILTER_STEP_COUNT; ++x)
        {
            vec2 offset = vec2(x, x) * radiusUV;
            TexArrayUV temp;
            temp.u = TexCoord_.x + offset.x;
            temp.v = TexCoord_.y + offset.y;
            temp.index = k;
            //temp = MakeTexArrayUV(temp);		// remove this to improve performance
            color += texture(tex_object_array, vec3(temp.u, temp.v, temp.index));
        }

        color /= COUNT;

        if (k == 0)
            color0 = color;
        else if (k == 1)
            color1 = color;
        else if (k == 2)
            color2 = color;
        else if (k == 3)
            color3 = color;
        else if (k == 4)
            color4 = color;
        else if (k == 5)
            color5 = color;
    }
}