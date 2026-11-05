#pragma once

struct jShader;
class jMaterial;

// Keep the shared static permutation cache untouched. The existing RHI shader
// cache owns graph variants keyed by injected code and permutation definitions.
jShader* ResolveMaterialMappingShader(jShader* base, const jMaterial* material);
