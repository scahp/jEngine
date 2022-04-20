#version 430 core

#preprocessor

precision mediump float;

layout (triangles) in;
layout (triangle_strip, max_vertices=18) out;

uniform mat4 OmniShadowMapVP[6];

out vec4 fragPos_;
out int Layer;

void main()
{
	for(int face=0;face<6;++face)
	{
		Layer = face;
		gl_ViewportIndex = face;
		for(int i=0;i<3;++i)
		{
			fragPos_ = gl_in[i].gl_Position;
			gl_Position = OmniShadowMapVP[face] * fragPos_;
			EmitVertex();
		}
		EndPrimitive();
	}
}