#include "pch.h"
#include "jShader.h"
#include "FileLoader/jFile.h"

bool jShader::IsRunningCheckUpdateShaderThread = false;
std::thread jShader::CheckUpdateShaderThread;
std::vector<jShader*> jShader::WaitForUpdateShaders;
std::map<const jShader*, std::vector<size_t>> jShader::gConnectedPipelineStateHash;

jShader::~jShader()
{
	delete CompiledShader;
}

void jShader::StartAndRunCheckUpdateShaderThread()
{
    if (!GUseRealTimeShaderUpdate)
        return;

    static std::atomic_bool HasNewWaitForupdateShaders(false);
    static jMutexLock Lock;
    if (!CheckUpdateShaderThread.joinable())
    {
        IsRunningCheckUpdateShaderThread = true;
        CheckUpdateShaderThread = std::thread([]()
        {
            while (IsRunningCheckUpdateShaderThread)
            {
                std::vector<jShader*> Shaders = g_rhi->GetAllShaders();

                static int32 CurrentIndex = 0;
                if (Shaders.size() > 0)
                {
                    jScopedLock s(&Lock);
                    for (int32 i = 0; i < GMaxCheckCountForRealTimeShaderUpdate; ++i, ++CurrentIndex)
                    {
                        if (CurrentIndex >= (int32)Shaders.size())
                        {
                            CurrentIndex = 0;
                        }

                        if (Shaders[CurrentIndex]->UpdateShader())
                        {
                            WaitForUpdateShaders.push_back(Shaders[CurrentIndex]);
                        }
                    }

                    if (WaitForUpdateShaders.size() > 0)
                        HasNewWaitForupdateShaders.store(true);
                }
                Sleep(GSleepMSForRealTimeShaderUpdate);
            }
        });
    }

    if (HasNewWaitForupdateShaders.load())
    {
        jScopedLock s(&Lock);

        HasNewWaitForupdateShaders.store(false);
        check(WaitForUpdateShaders.size() > 0);

        g_rhi->Flush();

        for (auto it = WaitForUpdateShaders.begin(); WaitForUpdateShaders.end() != it;)
        {
            jShader* Shader = *it;

            // Backup previous data
            jCompiledShader* PreviousCompiledShader = Shader->CompiledShader;
            auto PreviousPipelineStateHashes = gConnectedPipelineStateHash[Shader];
            gConnectedPipelineStateHash[Shader].clear();

            // Reset Shader
            Shader->CompiledShader = nullptr;
            g_rhi->ReleaseShader(Shader->ShaderInfo);

            // Try recreate shader
            if (g_rhi->CreateShaderInternal(Shader, Shader->ShaderInfo))
            {
                // Remove Pipeline which is connected with this shader
                for (auto PipelineStateHash : PreviousPipelineStateHashes)
                {
                    g_rhi->RemovePipelineStateInfo(PipelineStateHash);
                }

                // Release previous compiled shader
                delete PreviousCompiledShader;

                it = WaitForUpdateShaders.erase(it);
            }
            else
            {
                // Restore shader data
                Shader->CompiledShader = PreviousCompiledShader;
                gConnectedPipelineStateHash[Shader] = PreviousPipelineStateHashes;
                g_rhi->AddShader(Shader->ShaderInfo, Shader);

                ++it;
            }
        }
    }
}


void jShader::ReleaseCheckUpdateShaderThread()
{
    if (CheckUpdateShaderThread.joinable())
    {
        IsRunningCheckUpdateShaderThread = false;
        CheckUpdateShaderThread.join();
    }
}

bool jShader::UpdateShader()
{
	auto checkTimeStampFunc = [this](const char* filename) -> uint64
	{
		if (filename)
			return jFile::GetFileTimeStamp(filename);
		return 0;
	};

    // Check the state of shader file or any include shader file
    uint64 currentTimeStamp = checkTimeStampFunc(ShaderInfo.GetShaderFilepath().ToStr());
    for(auto Name : ShaderInfo.GetIncludeShaderFilePaths())
    {
        currentTimeStamp = Max(checkTimeStampFunc(Name.ToStr()), currentTimeStamp);
    }
	
	if (currentTimeStamp <= 0)
		return false;
	
	if (TimeStamp == 0)
	{
		TimeStamp = currentTimeStamp;
		return false;
	}

	if (TimeStamp >= currentTimeStamp)
		return false;

	TimeStamp = currentTimeStamp;

	return true;
}

void jShader::Initialize()
{
	verify(g_rhi->CreateShaderInternal(this, ShaderInfo));
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderForwardPixelShader
	, "ForwardPS"
	, "Resource/Shaders/hlsl/shader_fs.hlsl"
	, ""
	, "main"
	, EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderGBufferVertexShader
    , "GBufferVS"
    , "Resource/Shaders/hlsl/gbuffer_vs.hlsl"
    , ""
	, "main"
    , EShaderAccessStageFlag::VERTEX)

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderGBufferPixelShader
	, "GBufferPS"
	, "Resource/Shaders/hlsl/gbuffer_fs.hlsl"
	, ""
	, "main"
	, EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderDirectionalLightPixelShader
    , "DirectionalLightShaderPS"
    , "Resource/Shaders/hlsl/directionallight_fs.hlsl"
    , ""
	, "main"
    , EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderPointLightPixelShader
    , "PointLightShaderPS"
    , "Resource/Shaders/hlsl/pointlight_fs.hlsl"
    , ""
	, "main"
    , EShaderAccessStageFlag::FRAGMENT)

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderSpotLightPixelShader
    , "SpotLightShaderPS"
    , "Resource/Shaders/hlsl/spotlight_fs.hlsl"
    , ""
	, "main"
    , EShaderAccessStageFlag::FRAGMENT)
