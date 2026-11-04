struct VSOutput
{
    float4 Pos : SV_POSITION;
    float Opacity : TEXCOORD0;
};

void main(VSOutput input) : SV_TARGET
{
    if (input.Opacity <= frac(dot(input.Pos.xy, float2(0.754877666, 0.569840296)))) discard;
}
