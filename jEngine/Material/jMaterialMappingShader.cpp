#include "pch.h"
#include "jMaterialMappingShader.h"
#include "jMaterial.h"
#include "Shader/jShader.h"

jShader* ResolveMaterialMappingShader(jShader* base, const jMaterial* material)
{
    if (!base || !material || !material->MappingProgram || !material->MappingProgram->Active)
        return base;
    jShaderInfo info = base->ShaderInfo;
    std::string definitions = info.GetPreProcessors().ToStr();
    base->GetPermutationDefines(definitions);
    info.SetPreProcessors(jName(definitions.c_str()));
    std::string injected = info.GetInjectedShaderText().ToStr();
    injected += "\n";
    injected += material->MappingProgram->Hlsl;
    info.SetInjectedShaderText(jName(injected.c_str()));
    auto* shader = g_rhi->CreateShader(info);
    if (!shader || !shader->GetCompiledShader())
    {
        std::cerr << "Material mapping shader creation failed; base shader retained: "
            << info.GetShaderFilepath().ToStr() << '\n';
        return base;
    }
    return shader;
}
