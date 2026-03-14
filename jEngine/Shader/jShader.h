#pragma once
#include "Core/jName.h"
#include "Core/TInstantStruct.h"

// Shader Permutation Define
#define DECLARE_DEFINE(Name, ...) \
class Name \
{ \
public: \
    static constexpr char DefineName[] = #Name; \
    static constexpr int32 Value[] = { __VA_ARGS__ }; \
    static constexpr int32 Count = _countof(Value); \
};

// Shader Permutation. by using Shader Permutation Define, generate permutation of defines and convert it to permutation id.
template <typename ... T>
class jPermutation
{
public:
	template <typename K>
	using TFindClass = jPermutation;

    static constexpr int32 MaxPermutationCount = 1;

    template <typename K>
    constexpr int32 GetDefineCount() { return 1; }

    template <typename K>
    int32 Get() const { return int32(); }

    template <typename K>
    int32 GetIndex() const { return int32(); }

    template <typename K>
    void SetIndex(int32) { }

    constexpr int32 GetPermutationId() const { return 0; }
    void SetFromPermutationId(int32) {}
    void GetPermutationDefines(std::string&) const {}

    constexpr int32 GetLocalPermutationId() const { return 0; }
	constexpr int32 GetValueIndex() const { return 0; }
	constexpr void SetLocalPermutationId(int32) { }
	constexpr void SetValueIndex(int32) { }
};

template <typename T, typename ... T1>
class jPermutation<T, T1...> : public jPermutation<T1...>
{
public:
    using Type = T;
    using Super = jPermutation<T1...>;
    using ThisClass = jPermutation<T, T1...>;
    
    // Find jPermutation class which have K as a Type.
    template <typename K> using TFindClass = typename std::conditional_t<std::is_same<T, K>::value, ThisClass, typename Super::template TFindClass<K>>;

    static constexpr int32 MaxPermutationCount = T::Count * Super::MaxPermutationCount;

    template <typename K>
    constexpr int32 GetDefineCount()
    {
        return TFindClass<K>::Count;
    }

    template <typename K>
    int32 Get() const
    {
		const TFindClass<K>& FoundClass = *this;
		return T::Value[FoundClass.GetValueIndex()];
    }

    template <typename K>
    int32 GetIndex() const
    {
		const TFindClass<K>& FoundClass = *this;
        return FoundClass.GetValueIndex();
    }

    template <typename K>
    void SetIndex(int32 value)
    {
        TFindClass<K>& FoundClass = *this;

		FoundClass.SetValueIndex(value);
		FoundClass.SetLocalPermutationId(value * TFindClass<K>::Super::MaxPermutationCount);
    }

    constexpr int32 GetPermutationId() const
    {
        return LocalPermutationId + Super::GetPermutationId();
    }

    void SetFromPermutationId(int32 permutationId)
    {
        ValueIndex = (permutationId / Super::MaxPermutationCount) % T::Count;
        Super::SetFromPermutationId(permutationId);
    }

    void GetPermutationDefines(std::string& OutDefines) const
    {
        OutDefines += "#define ";
        OutDefines += Type::DefineName;
        OutDefines += " ";
        OutDefines += std::to_string(T::Value[ValueIndex]);
        OutDefines += "\r\n";

        Super::GetPermutationDefines(OutDefines);
    }

	int32 GetLocalPermutationId() const { return LocalPermutationId; }
	int32 GetValueIndex() const { return ValueIndex; }
	void SetLocalPermutationId(int32 InValue) { LocalPermutationId = InValue; }
	void SetValueIndex(int32 InValue) { ValueIndex = InValue; }

	int32 LocalPermutationId = 0;
	int32 ValueIndex = 0;
};

struct jShaderInfo
{
    static void GetShaderTypeDefines(std::string& OutResult, EShaderAccessStageFlag InShaderType);

    jShaderInfo() = default;
    jShaderInfo(jName InName, jName InShaderFilepath, jName InPreProcessors, jName InEntryPoint, EShaderAccessStageFlag InShaderType)
        : Name(InName), ShaderFilepath(InShaderFilepath), PreProcessors(InPreProcessors), EntryPoint(InEntryPoint), ShaderType(InShaderType)
    {}

    void Initialize() {}

	FORCEINLINE size_t GetHash() const
	{
        if (Hash)
            return Hash;

	Hash = GETHASH_FROM_INSTANT_STRUCT(Name.GetNameHash(), ShaderFilepath.GetNameHash()
			, PreProcessors.GetNameHash(), InjectedShaderText.GetNameHash(), EntryPoint.GetNameHash(), ShaderType, PermutationId);
		return Hash;
	}
    mutable size_t Hash = 0;

    const jName& GetName() const { return Name; }
    const jName& GetShaderFilepath() const { return ShaderFilepath; }
    const jName& GetPreProcessors() const { return PreProcessors; }
    const jName& GetInjectedShaderText() const { return InjectedShaderText; }
    const jName& GetEntryPoint() const { return EntryPoint; }
    const EShaderAccessStageFlag GetShaderType() const { return ShaderType; }
    const uint32& GetPermutationId() const { return PermutationId; }
    const std::vector<jName>& GetIncludeShaderFilePaths() const { return IncludeShaderFilePaths; }

    void SetName(const jName& InName) { Name = InName; Hash = 0; }
    void SetShaderFilepath(const jName& InShaderFilepath) { ShaderFilepath = InShaderFilepath; Hash = 0; }
    void SetPreProcessors(const jName& InPreProcessors) { PreProcessors = InPreProcessors; Hash = 0; }
    void SetInjectedShaderText(const jName& InInjectedShaderText) { InjectedShaderText = InInjectedShaderText; Hash = 0; }
    void AddPreProcessor(const char* InDefine, const char* InValue);
    void SetEntryPoint(const jName& InEntryPoint) { EntryPoint = InEntryPoint; Hash = 0; }
    void SetShaderType(const EShaderAccessStageFlag InShaderType) { ShaderType = InShaderType; Hash = 0; }
    void SetPermutationId(const uint32 InPermutationId) { PermutationId = InPermutationId; Hash = 0; }
    void SetIncludeShaderFilePaths(const std::vector<jName>& InPaths) { IncludeShaderFilePaths = InPaths; }

private:
	jName Name;
    jName PreProcessors;
    jName InjectedShaderText;
    jName EntryPoint = jNameStatic("main");
    jName ShaderFilepath;
    std::vector<jName> IncludeShaderFilePaths;
    EShaderAccessStageFlag ShaderType = (EShaderAccessStageFlag)0;
    uint32 PermutationId = 0;
};

struct jCompiledShader
{
	virtual ~jCompiledShader() {}
};

struct jViewShaderParameters;
struct jRenderObjectShaderParameters;
struct jMaterialShaderParameters;
struct jSceneTexturesShaderParameters;
struct jSceneSubpassInputShaderParameters;
struct jDirectionalLightShaderParameters;
struct jDirectionalLightOnlyShaderParameters;
struct jDirectionalLightIBLShaderParameters;
struct jPointLightShaderParameters;
struct jPointLightOnlyShaderParameters;
struct jSpotLightShaderParameters;
struct jSpotLightOnlyShaderParameters;
struct jLightVolumeVertexShaderParameters;
struct jBilateralFilteringCSParameters;

struct jShader : public std::enable_shared_from_this<jShader>
{
    static bool IsRunningCheckUpdateShaderThread;
    static std::thread CheckUpdateShaderThread;
    static std::vector<jShader*> WaitForUpdateShaders;
    static std::map<const jShader*, std::vector<size_t>> gConnectedPipelineStateHash;

	jShader()
	{}
    jShader(const jShaderInfo& shaderInfo)
        : ShaderInfo(shaderInfo)
    { }
	virtual ~jShader();

	static void StartAndRunCheckUpdateShaderThread();
    static void ReleaseCheckUpdateShaderThread();

    bool UpdateShader();
    virtual void Initialize();
    virtual bool IsInvalidated() const { return false; }

	uint64 TimeStamp = 0;
	jShaderInfo ShaderInfo;

	jCompiledShader* GetCompiledShader() const
	{
		return CompiledShader;
	}

    virtual void SetPermutationId(int32 InPermutaitonId) { }
    virtual int32 GetPermutationId() const { return 0; }
    virtual int32 GetPermutationCount() const { return 1; };
    virtual void GetPermutationDefines(std::string& OutResult) const { }
    jCompiledShader* CompiledShader = nullptr;
};

template <typename... T>
struct TShaderParameterSetList
{
};

template <typename TShaderParameterSet>
void AppendShaderParameterSetToInfo(jShaderInfo& InOutShaderInfo, uint32 InSpace);

namespace jShaderBindlessSet
{
    template <typename TBindlessSet>
    void AppendToShaderInfo(jShaderInfo& InOutShaderInfo, uint32 InBaseSpace);

    template <typename TBindlessSet>
    uint32 GetSpaceCount();
}

struct jShaderParameterBinder
{
    explicit jShaderParameterBinder(jShaderInfo& InInfo, uint32 InStartSpace = 0)
        : ShaderInfo(InInfo)
        , NextSpace(InStartSpace)
    {
    }

    template <typename TShaderParameterSet>
    void Add()
    {
        AppendShaderParameterSetToInfo<TShaderParameterSet>(ShaderInfo, NextSpace++);
    }

    template <typename TBindlessSet>
    void AddBindless()
    {
        jShaderBindlessSet::AppendToShaderInfo<TBindlessSet>(ShaderInfo, NextSpace);
        NextSpace += jShaderBindlessSet::GetSpaceCount<TBindlessSet>();
    }

    uint32 GetNextSpace() const { return NextSpace; }

private:
    jShaderInfo& ShaderInfo;
    uint32 NextSpace = 0;
};

template <typename ShaderClass, typename = int>
struct THasShaderParameterSets : std::false_type
{
};

template <typename ShaderClass>
struct THasShaderParameterSets<ShaderClass, decltype((void)sizeof(typename ShaderClass::ShaderParameterSets), 0)> : std::true_type
{
};

template <typename ShaderClass, typename = int>
struct THasAppendConditionalShaderParameterSets : std::false_type
{
};

template <typename ShaderClass>
struct THasAppendConditionalShaderParameterSets<ShaderClass, decltype((void)(&ShaderClass::AppendConditionalShaderParameterSets), 0)> : std::true_type
{
};

template <typename ShaderClass, typename = int>
struct THasShaderParameterSetBaseSpace : std::false_type
{
};

template <typename ShaderClass>
struct THasShaderParameterSetBaseSpace<ShaderClass, decltype((void)ShaderClass::ShaderParameterSetBaseSpace, 0)> : std::true_type
{
};

template <typename TShaderParameterSetListType>
struct TAppendShaderParameterSetListToInfo;

template <typename... TShaderParameterSetTypes>
struct TAppendShaderParameterSetListToInfo<TShaderParameterSetList<TShaderParameterSetTypes...>>
{
    static void Append(jShaderParameterBinder& InOutBinder)
    {
        int32 Dummy[] = { 0, (InOutBinder.Add<TShaderParameterSetTypes>(), 0)... };
        (void)Dummy;
    }
};

template <typename ShaderClass, typename = int>
struct TShaderParameterSetBaseSpaceValue
{
    static constexpr uint32 Value = 0;
};

template <typename ShaderClass>
struct TShaderParameterSetBaseSpaceValue<ShaderClass, decltype((void)ShaderClass::ShaderParameterSetBaseSpace, 0)>
{
    static constexpr uint32 Value = ShaderClass::ShaderParameterSetBaseSpace;
};

template <typename ShaderClass>
inline typename std::enable_if<THasShaderParameterSets<ShaderClass>::value, void>::type ApplyShaderInfoCustomization(jShaderInfo& InOutShaderInfo)
{
    constexpr uint32 BaseSpace = TShaderParameterSetBaseSpaceValue<ShaderClass>::Value;
    jShaderParameterBinder Binder(InOutShaderInfo, BaseSpace);
    TAppendShaderParameterSetListToInfo<typename ShaderClass::ShaderParameterSets>::Append(Binder);

    if constexpr (THasAppendConditionalShaderParameterSets<ShaderClass>::value)
    {
        typename ShaderClass::ShaderPermutation Permutation;
        Permutation.SetFromPermutationId((int32)InOutShaderInfo.GetPermutationId());
        ShaderClass::AppendConditionalShaderParameterSets(Binder, Permutation);
    }
}

template <typename T>
struct TShaderCustomizationMissing : std::false_type
{
};

template <typename ShaderClass>
inline typename std::enable_if<!THasShaderParameterSets<ShaderClass>::value, void>::type ApplyShaderInfoCustomization(jShaderInfo&)
{
    static_assert(TShaderCustomizationMissing<ShaderClass>::value, "Shader must declare ShaderParameterSets.");
}

#define DECLARE_SHADER_PARAMETER_SETS(...) \
    using ShaderParameterSets = TShaderParameterSetList<__VA_ARGS__>;

#define DECLARE_SHADER_WITH_PERMUTATION_EX(ShaderClass, BaseClass, PermutationVariable) \
public: \
    static ShaderClass* Shaders[ShaderClass::ShaderPermutation::MaxPermutationCount]; \
    static jShaderInfo GShaderInfo; \
    static ShaderClass* CreateShader(const ShaderClass::ShaderPermutation& InPermutation); \
    using BaseClass::BaseClass; \
    virtual void SetPermutationId(int32 InPermutaitonId) override { PermutationVariable.SetFromPermutationId(InPermutaitonId); } \
    virtual int32 GetPermutationId() const override { return PermutationVariable.GetPermutationId(); } \
    virtual int32 GetPermutationCount() const override { return ShaderClass::ShaderPermutation::MaxPermutationCount; } \
    virtual void GetPermutationDefines(std::string& OutResult) const { PermutationVariable.GetPermutationDefines(OutResult); }

#define DECLARE_SHADER_WITH_PERMUTATION(ShaderClass, PermutationVariable) \
    DECLARE_SHADER_WITH_PERMUTATION_EX(ShaderClass, jShader, PermutationVariable)

#define IMPLEMENT_SHADER_WITH_PERMUTATION(ShaderClass, Name, Filepath, Preprocessor, EntryName, ShaderAccesssStageFlag) \
ShaderClass* ShaderClass::Shaders[ShaderClass::ShaderPermutation::MaxPermutationCount] = { nullptr, }; \
jShaderInfo ShaderClass::GShaderInfo( \
    jNameStatic(Name), \
    jNameStatic(Filepath), \
    jNameStatic(Preprocessor), \
    jNameStatic(EntryName), \
    ShaderAccesssStageFlag \
); \
ShaderClass* ShaderClass::CreateShader(const ShaderClass::ShaderPermutation& InPermutation) \
{ \
    const auto PermutationId = InPermutation.GetPermutationId(); \
    if (Shaders[PermutationId]) \
        return Shaders[PermutationId]; \
    jShaderInfo TempShaderInfo = GShaderInfo; \
    TempShaderInfo.SetPermutationId(PermutationId); \
    ApplyShaderInfoCustomization<ShaderClass>(TempShaderInfo); \
    auto NewShader = g_rhi->CreateShader<ShaderClass>(TempShaderInfo); /* Don't need to care about thread-safe because CreateShader will care about this. */ \
    NewShader->Permutation = InPermutation; \
    Shaders[PermutationId] = NewShader; \
    return Shaders[PermutationId]; \
}

struct jShaderForwardPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jDirectionalLightShaderParameters,
        jPointLightShaderParameters,
        jSpotLightShaderParameters,
        jRenderObjectShaderParameters,
        jMaterialShaderParameters)

	DECLARE_DEFINE(USE_VARIABLE_SHADING_RATE, 0, 1);
    DECLARE_DEFINE(USE_REVERSEZ, 0, 1);

	using ShaderPermutation = jPermutation<USE_VARIABLE_SHADING_RATE, USE_REVERSEZ>;
	ShaderPermutation Permutation;

	DECLARE_SHADER_WITH_PERMUTATION(jShaderForwardPixelShader, Permutation)
};

struct jShaderGBufferVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters)

    DECLARE_DEFINE(USE_VERTEX_COLOR, 0, 1);
    DECLARE_DEFINE(USE_VERTEX_BITANGENT, 0, 1);
    DECLARE_DEFINE(USE_ALBEDO_TEXTURE, 0, 1);
    DECLARE_DEFINE(USE_SPHERICAL_MAP, 0, 1);

    using ShaderPermutation = jPermutation<USE_VERTEX_COLOR, USE_VERTEX_BITANGENT, USE_ALBEDO_TEXTURE, USE_SPHERICAL_MAP>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderGBufferVertexShader, Permutation)
};

struct jShaderGBufferPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters,
        jMaterialShaderParameters)

    DECLARE_DEFINE(USE_VERTEX_COLOR, 0, 1);
    DECLARE_DEFINE(USE_ALBEDO_TEXTURE, 0, 1);
    DECLARE_DEFINE(USE_SRGB_ALBEDO_TEXTURE, 0, 1);
    DECLARE_DEFINE(USE_VARIABLE_SHADING_RATE, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);
    
    using ShaderPermutation = jPermutation<USE_VERTEX_COLOR, USE_ALBEDO_TEXTURE, USE_SRGB_ALBEDO_TEXTURE, USE_VARIABLE_SHADING_RATE, USE_PBR>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderGBufferPixelShader, Permutation)
};

struct jShaderDebugObjectVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderDebugObjectVertexShader, Permutation)
};

struct jShaderDebugObjectPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters,
        jMaterialShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderDebugObjectPixelShader, Permutation)
};

struct jShaderFullscreenQuadVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS()

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderFullscreenQuadVertexShader, Permutation)
};

struct jShaderForwardVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jDirectionalLightShaderParameters,
        jPointLightShaderParameters,
        jSpotLightShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderForwardVertexShader, Permutation)
};

struct jShaderForwardInstancingVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jDirectionalLightShaderParameters,
        jPointLightShaderParameters,
        jSpotLightShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderForwardInstancingVertexShader, Permutation)
};

struct jShaderOmniShadowVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jPointLightOnlyShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderOmniShadowVertexShader, Permutation)
};

struct jShaderOmniShadowPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jPointLightOnlyShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderOmniShadowPixelShader, Permutation)
};

struct jShaderSpotShadowVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jSpotLightOnlyShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderSpotShadowVertexShader, Permutation)
};

struct jShaderDirectionalShadowVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jDirectionalLightOnlyShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderDirectionalShadowVertexShader, Permutation)
};

struct jShaderShadowPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS()

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderShadowPixelShader, Permutation)
};

struct jShaderShadowInstancingVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jDirectionalLightOnlyShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderShadowInstancingVertexShader, Permutation)
};

struct jShaderDirectionalLightPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jDirectionalLightShaderParameters,
        jDirectionalLightIBLShaderParameters)

    DECLARE_DEFINE(USE_SUBPASS, 0, 1);
    DECLARE_DEFINE(USE_SHADOW_MAP, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);

    using ShaderPermutation = jPermutation<USE_SUBPASS, USE_SHADOW_MAP, USE_PBR>;
    ShaderPermutation Permutation;

    static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation);

    DECLARE_SHADER_WITH_PERMUTATION(jShaderDirectionalLightPixelShader, Permutation)
};

struct jShaderPointLightPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jPointLightShaderParameters,
        jLightVolumeVertexShaderParameters)

    DECLARE_DEFINE(USE_SUBPASS, 0, 1);
    DECLARE_DEFINE(USE_SHADOW_MAP, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);

    using ShaderPermutation = jPermutation<USE_SUBPASS, USE_SHADOW_MAP, USE_PBR>;
    ShaderPermutation Permutation;

    static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation);

    DECLARE_SHADER_WITH_PERMUTATION(jShaderPointLightPixelShader, Permutation)
};

struct jShaderPointLightVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jPointLightShaderParameters,
        jLightVolumeVertexShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderPointLightVertexShader, Permutation)
};

struct jShaderSpotLightPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jSpotLightShaderParameters,
        jLightVolumeVertexShaderParameters)

    DECLARE_DEFINE(USE_SUBPASS, 0, 1);
    DECLARE_DEFINE(USE_SHADOW_MAP, 0, 1);
    DECLARE_DEFINE(USE_REVERSEZ, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);

    using ShaderPermutation = jPermutation<USE_SUBPASS, USE_SHADOW_MAP, USE_REVERSEZ, USE_PBR>;
    ShaderPermutation Permutation;

    static void AppendConditionalShaderParameterSets(jShaderParameterBinder& InOutBinder, const ShaderPermutation& InPermutation);

    DECLARE_SHADER_WITH_PERMUTATION(jShaderSpotLightPixelShader, Permutation)
};

struct jShaderSpotLightVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jSpotLightShaderParameters,
        jLightVolumeVertexShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderSpotLightVertexShader, Permutation)
};

struct jShaderBilateralComputeShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jBilateralFilteringCSParameters)

    DECLARE_DEFINE(USE_GAUSSIAN_INSTEAD, 0, 1);

    using ShaderPermutation = jPermutation<USE_GAUSSIAN_INSTEAD>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderBilateralComputeShader, Permutation)
};


struct jGraphicsPipelineShader
{
    jShader* VertexShader = nullptr;
    jShader* GeometryShader = nullptr;
    jShader* PixelShader = nullptr;

    size_t GetHash() const
    {
        size_t hash = 0;
        if (VertexShader)
            hash ^= VertexShader->ShaderInfo.GetHash();
        
        if (GeometryShader)
            hash ^= GeometryShader->ShaderInfo.GetHash();
        
        if (PixelShader)
            hash ^= PixelShader->ShaderInfo.GetHash();

        return hash;
    }
};

struct jRaytracingPipelineShader
{
    static constexpr int32 MaxNumOfShaders = 4;

    jShader* RaygenShader = nullptr;
    jShader* ClosestHitShader = nullptr;
    jShader* AnyHitShader = nullptr;
    jShader* MissShader = nullptr;

    std::wstring RaygenEntryPoint;
    std::wstring ClosestHitEntryPoint;
    std::wstring AnyHitEntryPoint;
    std::wstring MissEntryPoint;

    std::wstring HitGroupName;

    size_t GetHash() const;
};

struct jRaytracingPipelineData
{
    int32 MaxAttributeSize = 2 * sizeof(float);	    // float2 barycentrics
    int32 MaxPayloadSize = 4 * sizeof(float);		// float4 color
    int32 MaxTraceRecursionDepth = 1;

    size_t GetHash() const;
};
