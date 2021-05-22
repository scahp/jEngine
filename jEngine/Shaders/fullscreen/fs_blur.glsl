#version 330 core
precision mediump float;

uniform sampler2D tex_object;
uniform vec2 PixelSize;
uniform float IsVertical;
uniform float MaxDist;

#define FILTER_STEP_COUNT 5.0
#define FilterSize vec2(FILTER_STEP_COUNT, FILTER_STEP_COUNT)
#define COUNT (FILTER_STEP_COUNT * 2.0 + 1.0)

in vec2 TexCoord_;
out vec4 FragColor;

void main()
{
    vec2 radiusUV = (FilterSize * PixelSize) / FILTER_STEP_COUNT;

    if (IsVertical > 0.0)
    {
        radiusUV = vec2(0.0, radiusUV.y);
    }
    else
    {
        radiusUV = vec2(radiusUV.x, 0.0);
    }

    vec4 color = vec4(0);
    for (float x = -FILTER_STEP_COUNT; x <= FILTER_STEP_COUNT; ++x)
	{
        vec2 offset = vec2(x, x) * radiusUV;
        vec2 tex = TexCoord_ + offset;
        
        if (tex.x < 0.0 || tex.x > 1.0 || tex.y < 0.0 || tex.y > 1.0)
        {
            color.x += exp(MaxDist);
            color.y += exp(MaxDist * MaxDist);
            color.w += 1.0;
        }
        else
        {
            color += texture(tex_object, tex);
        }
    }

    FragColor = color / COUNT;
}