#include "common.glsl"

precision mediump float;

attribute vec3 Pos;

uniform mat4 MVP;
uniform mat4 M;

varying vec3 Pos_;

void main()
{
    gl_Position = MVP * vec4(Pos, 1.0);
    Pos_ = TransformPos(M, Pos);
}