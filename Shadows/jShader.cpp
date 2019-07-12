#include "pch.h"
#include "jShader.h"

//////////////////////////////////////////////////////////////////////////
// jShaderInfo
std::map<std::string, jShaderInfo> jShaderInfo::s_ShaderInfoMap;
void jShaderInfo::AddShaderInfo(const jShaderInfo& shaderInfo)
{
	s_ShaderInfoMap.insert(std::make_pair(shaderInfo.name, shaderInfo));
}

void jShaderInfo::CreateShaders()
{
	for (auto& it : s_ShaderInfoMap)
		jShader::CreateShader(it.second);
}

struct jShaderInfoCreation
{
	jShaderInfoCreation()
	{
		// Declare shader
		DECLARE_SHADER_VS_FS("Simple", "Shaders/color_only_vs.glsl", "Shaders/color_only_fs.glsl");
		DECLARE_SHADER_VS_FS("ShadowMap", "Shaders/vs_shadowMap.glsl", "Shaders/fs_shadowMap.glsl");
		DECLARE_SHADER_VS_FS("Base", "Shaders/shadowmap/vs.glsl", "Shaders/shadowmap/fs.glsl");
		DECLARE_SHADER_VS_FS("ShadowMapOmni", "Shaders/shadowmap/vs_omniDirectionalShadowMap.glsl", "Shaders/shadowmap/fs_omniDirectionalShadowMap.glsl");
		DECLARE_SHADER_VS_FS("ShadowVolumeBase", "Shaders/shadowvolume/vs.glsl", "Shaders/shadowvolume/fs.glsl");
		DECLARE_SHADER_VS_FS("ShadowVolumeInfinityFar", "Shaders/shadowvolume/vs_infinityFar.glsl", "Shaders/shadowvolume/fs_infinityFar.glsl");
		DECLARE_SHADER_VS_FS("ExpDeepShadowMapGen", "Shaders/shadowmap/vs_expDeepShadowMap.glsl", "Shaders/shadowmap/fs_expDeepShadowMap.glsl");
		DECLARE_SHADER_VS_FS("DeepShadowMapGen", "Shaders/shadowmap/vs_shadowMap.glsl", "Shaders/shadowmap/fs_deepShadowMap.glsl");
		DECLARE_SHADER_VS_FS_WITH_OPTION("Hair", "shaders/shadowmap/vs_hair.glsl", "shaders/shadowmap/fs_hair.glsl", true, true);
		DECLARE_SHADER_VS_FS_WITH_OPTION("ExpDeepShadowFull", "shaders/fullscreen/vs_deepshadow.glsl", "shaders/fullscreen/fs_expdeepshadow.glsl", true, true);
		DECLARE_SHADER_VS_FS("DeepShadowFull", "shaders/fullscreen/vs_deepshadow.glsl", "shaders/fullscreen/fs_deepshadow.glsl");
		DECLARE_SHADER_VS_FS("DeepShadowAA", "shaders/fullscreen/vs_deepshadow.glsl", "shaders/fullscreen/fs_deepshadow_aa.glsl");
		DECLARE_SHADER_VS_FS_WITH_OPTION("ExpDeferred", "shaders/shadowmap/vs_deferred.glsl", "shaders/shadowmap/fs_expDeferred.glsl", true, true);
		DECLARE_SHADER_VS_FS_WITH_OPTION("Deferred", "shaders/shadowmap/vs_deferred.glsl", "shaders/shadowmap/fs_deferred.glsl", true, true);
		DECLARE_SHADER_CS("cs", "Shaders/compute/compute_example.glsl");
		DECLARE_SHADER_CS("cs_sort", "Shaders/compute/compute_sort_linkedlist.glsl");
		DECLARE_SHADER_CS("cs_link", "Shaders/compute/compute_link_linkedlist.glsl");

		DECLARE_SHADER_VS_FS("ShadowGen_SSM", "shaders/shadowmap/vs_shadowMap.glsl", "shaders/shadowmap/fs_shadowMap.glsl");
		DECLARE_SHADER_VS_GS_FS("ShadowGen_Omni_SSM", "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/gs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/fs_omniDirectionalShadowMap.glsl");
		DECLARE_SHADER_VS_FS("SSM", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl");
		DECLARE_SHADER_VS_FS_WITH_OPTION_MORE("PCF", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl", false, false, "\r\n#define USE_PCF 1");
		DECLARE_SHADER_VS_FS_WITH_OPTION_MORE("PCF_Poisson", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl", false, false, "\r\n#define USE_PCF 1\r\n#define USE_POISSON_SAMPLE 1");

		DECLARE_SHADER_VS_FS_WITH_OPTION_MORE("PCSS", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl", false, false, "\r\n#define USE_PCSS 1");
		DECLARE_SHADER_VS_FS_WITH_OPTION_MORE("PCSS_Poisson", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl", false, false, "\r\n#define USE_PCSS 1\r\n#define USE_POISSON_SAMPLE 1");

		DECLARE_SHADER_VS_FS("ShadowGen_VSM", "shaders/shadowmap/vs_varianceShadowMap.glsl", "shaders/shadowmap/fs_varianceShadowMap.glsl");
		DECLARE_SHADER_VS_GS_FS("ShadowGen_Omni_VSM", "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/gs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/fs_omniDirectionalShadowMap.glsl");

		DECLARE_SHADER_VS_FS_WITH_OPTION_MORE("VSM", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl", false, false, "\r\n#define USE_VSM 1");
		DECLARE_SHADER_VS_FS_WITH_OPTION_MORE("ESM", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl", false, false, "\r\n#define USE_ESM 1");
		DECLARE_SHADER_VS_FS_WITH_OPTION_MORE("EVSM", "shaders/shadowmap/vs.glsl", "shaders/shadowmap/fs.glsl", false, false, "\r\n#define USE_EVSM 1");

		DECLARE_SHADER_VS_FS("Blur", "shaders/fullscreen/vs_blur.glsl", "shaders/fullscreen/fs_blur.glsl");
		DECLARE_SHADER_VS_FS("BlurOmni", "shaders/fullscreen/vs_omnidirectional_blur.glsl", "shaders/fullscreen/fs_omnidirectional_blur.glsl");

		DECLARE_SHADER_VS_FS("ShadowGen_ESM", "shaders/shadowmap/vs_varianceShadowMap.glsl", "shaders/shadowmap/fs_exponentialShadowMap.glsl");
		DECLARE_SHADER_VS_GS_FS("ShadowGen_Omni_ESM", "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/gs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/fs_omniDirectionalExponentialShadowMap.glsl");

		DECLARE_SHADER_VS_FS("ShadowGen_EVSM", "shaders/shadowmap/vs_EVSM.glsl", "shaders/shadowmap/fs_EVSM.glsl");
		DECLARE_SHADER_VS_GS_FS("ShadowGen_Omni_EVSM", "shaders/shadowmap/vs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/gs_omniDirectionalShadowMap.glsl", "shaders/shadowmap/fs_omniDirectionalEVSM.glsl");
	}
} s_shaderInfoCreation;
