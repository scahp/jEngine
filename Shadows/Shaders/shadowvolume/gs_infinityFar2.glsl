#version 430 core

#preprocessor

precision mediump float;

layout(triangles_adjacency) in;
layout(triangle_strip, max_vertices = 21) out;

uniform mat4 VP;
uniform mat4 MVP;
uniform mat4 M;
uniform int IsTwoSided;

in vec3 LightDirWS_[];

void EmitEdgeSilhouette(bool isFrontFace, int v0Index, int v1Index, int adjcency_vertIndex)
{
	vec3 m0 = gl_in[v0Index].gl_Position.xyz;
	vec3 m1 = gl_in[v1Index].gl_Position.xyz;

	vec3 l0 = LightDirWS_[v0Index];
	vec3 l1 = LightDirWS_[v1Index];

	vec3 adj_vert = gl_in[adjcency_vertIndex].gl_Position.xyz;

	vec3 c = cross(m1 - adj_vert, m0 - adj_vert);
	bool face = dot(c, (l0 + l1)) > 0;
	if (face != isFrontFace)
	{
		vec4 a0;
		vec4 a1;
		vec4 a2;
		vec4 a3;
		if (face)
		{
			a0 = VP * vec4(m0, 1.0);
			a1 = VP * vec4(m1, 1.0);
			a2 = VP * vec4(l0, 0.0);
			a3 = VP * vec4(l1, 0.0);
		}
		else
		{
			a0 = VP * vec4(m1, 1.0);
			a1 = VP * vec4(m0, 1.0);
			a2 = VP * vec4(l1, 0.0);
			a3 = VP * vec4(l0, 0.0);
		}

		gl_Position = a0;
		EmitVertex();
		gl_Position = a1;
		EmitVertex();
		gl_Position = a2;
		EmitVertex();
		EndPrimitive();
		gl_Position = a2;
		EmitVertex();
		gl_Position = a1;
		EmitVertex();
		gl_Position = a3;
		EmitVertex();
		EndPrimitive();
	}
}

void main()
{
	vec3 m0 = gl_in[0].gl_Position.xyz;
	vec3 m1 = gl_in[2].gl_Position.xyz;
	vec3 m2 = gl_in[4].gl_Position.xyz;

	vec3 ma0 = gl_in[1].gl_Position.xyz;
	vec3 ma1 = gl_in[3].gl_Position.xyz;
	vec3 ma2 = gl_in[5].gl_Position.xyz;

	vec3 r = cross(m1 - m0, m2 - m0);
	bool isFrontFace = dot(LightDirWS_[0], r) > 0;

	vec4 v0;
	vec4 v1;
	vec4 v2;
	if (isFrontFace)
	{
		v0 = VP * gl_in[0].gl_Position;
		v1 = VP * gl_in[2].gl_Position;
		v2 = VP * gl_in[4].gl_Position;

		gl_Position = v0;
		EmitVertex();
		gl_Position = v1;
		EmitVertex();
		gl_Position = v2;
		EmitVertex();
		EndPrimitive();

		if (IsTwoSided > 0)
		{
			v0 = VP * vec4(LightDirWS_[0], 0.0);
			v1 = VP * vec4(LightDirWS_[2], 0.0);
			v2 = VP * vec4(LightDirWS_[4], 0.0);

			gl_Position = v1;
			EmitVertex();
			gl_Position = v0;
			EmitVertex();
			gl_Position = v2;
			EmitVertex();
			EndPrimitive();
		}
	}
	else
	{
		v0 = VP * vec4(LightDirWS_[0], 0.0);
		v1 = VP * vec4(LightDirWS_[2], 0.0);
		v2 = VP * vec4(LightDirWS_[4], 0.0);

		gl_Position = v0;
		EmitVertex();
		gl_Position = v1;
		EmitVertex();
		gl_Position = v2;
		EmitVertex();
		EndPrimitive();
	}

	if (!isFrontFace || (IsTwoSided > 0))
	{
		EmitEdgeSilhouette(isFrontFace, 0, 2, 1);
		EmitEdgeSilhouette(isFrontFace, 2, 4, 3);
		EmitEdgeSilhouette(isFrontFace, 4, 0, 5);
	}
}