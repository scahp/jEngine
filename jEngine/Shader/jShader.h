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
			, PreProcessors.GetNameHash(), EntryPoint.GetNameHash(), ShaderType, PermutationId);
		return Hash;
	}
    mutable size_t Hash = 0;

    const jName& GetName() const { return Name; }
    const jName& GetShaderFilepath() const { return ShaderFilepath; }
    const jName& GetPreProcessors() const { return PreProcessors; }
    const jName& GetEntryPoint() const { return EntryPoint; }
    const EShaderAccessStageFlag GetShaderType() const { return ShaderType; }
    const uint32& GetPermutationId() const { return PermutationId; }
    const std::vector<jName>& GetIncludeShaderFilePaths() const { return IncludeShaderFilePaths; }

    void SetName(const jName& InName) { Name = InName; Hash = 0; }
    void SetShaderFilepath(const jName& InShaderFilepath) { ShaderFilepath = InShaderFilepath; Hash = 0; }
    void SetPreProcessors(const jName& InPreProcessors) { PreProcessors = InPreProcessors; Hash = 0; }
    void AddPreProcessor(const char* InDefine, const char* InValue);
    void SetEntryPoint(const jName& InEntryPoint) { EntryPoint = InEntryPoint; Hash = 0; }
    void SetShaderType(const EShaderAccessStageFlag InShaderType) { ShaderType = InShaderType; Hash = 0; }
    void SetPermutationId(const uint32 InPermutationId) { PermutationId = InPermutationId; Hash = 0; }
    void SetIncludeShaderFilePaths(const std::vector<jName>& InPaths) { IncludeShaderFilePaths = InPaths; }

private:
	jName Name;
    jName PreProcessors;
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

#define DECLARE_SHADER_WITH_PERMUTATION(ShaderClass, PermutationVariable) \
public: \
    static ShaderClass* Shaders[ShaderClass::ShaderPermutation::MaxPermutationCount]; \
    static jShaderInfo GShaderInfo; \
    static ShaderClass* CreateShader(const ShaderClass::ShaderPermutation& InPermutation); \
    using jShader::jShader; \
    virtual void SetPermutationId(int32 InPermutaitonId) override { PermutationVariable.SetFromPermutationId(InPermutaitonId); } \
    virtual int32 GetPermutationId() const override { return PermutationVariable.GetPermutationId(); } \
    virtual int32 GetPermutationCount() const override { return ShaderClass::ShaderPermutation::MaxPermutationCount; } \
    virtual void GetPermutationDefines(std::string& OutResult) const { PermutationVariable.GetPermutationDefines(OutResult); }

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
    auto NewShader = g_rhi->CreateShader<ShaderClass>(TempShaderInfo); /* Don't need to care about thread-safe because CreateShader will care about this. */ \
    NewShader->Permutation = InPermutation; \
    Shaders[PermutationId] = NewShader; \
    return Shaders[PermutationId]; \
}

struct jShaderForwardPixelShader : public jShader
{
	DECLARE_DEFINE(USE_VARIABLE_SHADING_RATE, 0, 1);
    DECLARE_DEFINE(USE_REVERSEZ, 0, 1);

	using ShaderPermutation = jPermutation<USE_VARIABLE_SHADING_RATE, USE_REVERSEZ>;
	ShaderPermutation Permutation;

	DECLARE_SHADER_WITH_PERMUTATION(jShaderForwardPixelShader, Permutation)
};

struct jShaderGBufferVertexShader : public jShader
{
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
    DECLARE_DEFINE(USE_VERTEX_COLOR, 0, 1);
    DECLARE_DEFINE(USE_ALBEDO_TEXTURE, 0, 1);
    DECLARE_DEFINE(USE_SRGB_ALBEDO_TEXTURE, 0, 1);
    DECLARE_DEFINE(USE_VARIABLE_SHADING_RATE, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);
    
    using ShaderPermutation = jPermutation<USE_VERTEX_COLOR, USE_ALBEDO_TEXTURE, USE_SRGB_ALBEDO_TEXTURE, USE_VARIABLE_SHADING_RATE, USE_PBR>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderGBufferPixelShader, Permutation)
};

struct jShaderDirectionalLightPixelShader : public jShader
{
    DECLARE_DEFINE(USE_SUBPASS, 0, 1);
    DECLARE_DEFINE(USE_SHADOW_MAP, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);

    using ShaderPermutation = jPermutation<USE_SUBPASS, USE_SHADOW_MAP, USE_PBR>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderDirectionalLightPixelShader, Permutation)
};

struct jShaderPointLightPixelShader : public jShader
{
    DECLARE_DEFINE(USE_SUBPASS, 0, 1);
    DECLARE_DEFINE(USE_SHADOW_MAP, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);

    using ShaderPermutation = jPermutation<USE_SUBPASS, USE_SHADOW_MAP, USE_PBR>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderPointLightPixelShader, Permutation)
};

struct jShaderSpotLightPixelShader : public jShader
{
    DECLARE_DEFINE(USE_SUBPASS, 0, 1);
    DECLARE_DEFINE(USE_SHADOW_MAP, 0, 1);
    DECLARE_DEFINE(USE_REVERSEZ, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);

    using ShaderPermutation = jPermutation<USE_SUBPASS, USE_SHADOW_MAP, USE_REVERSEZ, USE_PBR>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderSpotLightPixelShader, Permutation)
};

struct jShaderBilateralComputeShader : public jShader
{
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