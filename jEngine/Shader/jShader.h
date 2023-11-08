#pragma once
#include "Core/jName.h"

// Shader Permutation Define
#define DECLARE_DEFINE(Name, ...) \
class Name \
{ \
public: \
    static constexpr char DefineName[] = #Name; \
    static constexpr int Value[] = { __VA_ARGS__ }; \
    static constexpr int Count = _countof(Value); \
};

// Shader Permutation. by using Shader Permutation Define, generate permutation of defines and convert it to permutation id.
template <typename ... T>
class jPermutation
{
public:
    static int GetPermutationCount() { return 1; }

    template <typename K>
    static int GetDefineCount() { return 1; }

    template <typename K>
    int Get() const { return int(); }

    template <typename K>
    int GetIndex() const { return int(); }

    template <typename K>
    void SetIndex(int) { }

    int GetPermutationId() const { return 0; }
    void SetFromPermutationId(int) {}
    void GetPermutationDefines(std::string&) const {}
};

template <typename T, typename ... T1>
class jPermutation<T, T1...> : public jPermutation<T1...>
{
public:
    using Type = T;
    using Super = jPermutation<T1...>;

    static int GetPermutationCount()
    {
        return T::Count * Super::GetPermutationCount();
    }

    template <typename K>
    static int GetDefineCount()
    {
        if (std::is_same<T, K>::value)
            return T::Count;

        return Super::template GetDefineCount<K>();
    }

    template <typename K>
    int Get() const
    {
        if (std::is_same<T, K>::value)
            return T::Value[ValueIndex];

        return Super::template Get<K>();
    }

    template <typename K>
    int GetIndex() const
    {
        if (std::is_same<T, K>::value)
            return ValueIndex;

        return Super::template GetIndex<K>();
    }

    template <typename K>
    void SetIndex(int value)
    {
        if (std::is_same<T, K>::value)
        {
            ValueIndex = value;
            return;
        }

        return jPermutation<T1...>::template SetIndex<K>(value);
    }

    int GetPermutationId() const
    {
        return ValueIndex * Super::GetPermutationCount() + Super::GetPermutationId();
    }

    void SetFromPermutationId(int permutationId)
    {
        ValueIndex = (permutationId / Super::GetPermutationCount()) % T::Count;
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

    int ValueIndex = 0;
};

struct jShaderInfo
{
    jShaderInfo() = default;
    jShaderInfo(jName InName, jName InShaderFilepath, jName InPreProcessors, jName InEntryPoint, EShaderAccessStageFlag InShaderType)
        : Name(InName), ShaderFilepath(InShaderFilepath), PreProcessors(InPreProcessors), EntryPoint(InEntryPoint), ShaderType(InShaderType)
    {}

    void Initialize() {}

	FORCEINLINE size_t GetHash() const
	{
        if (Hash)
            return Hash;

		{
			if (Name.IsValid())
				Hash = CityHash64WithSeed(Name, Hash);
            if (ShaderFilepath.IsValid())
                Hash = CityHash64WithSeed(ShaderFilepath, Hash);
            if (PreProcessors.IsValid())
                Hash = CityHash64WithSeed(PreProcessors, Hash);
            if (EntryPoint.IsValid())
                Hash = CityHash64WithSeed(EntryPoint, Hash);
            Hash = CityHash64WithSeed((uint64)ShaderType, Hash);
            Hash = CityHash64WithSeed((uint64)PermutationId, Hash);
		}
		return Hash;
	}
    mutable size_t Hash = 0;

    const jName& GetName() const { return Name; }
    const jName& GetShaderFilepath() const { return ShaderFilepath; }
    const jName& GetPreProcessors() const { return PreProcessors; }
    const jName& GetEntryPoint() const { return EntryPoint; }
    const EShaderAccessStageFlag GetShaderType() const { return ShaderType; }
    const uint32& GetPermutationId() const { return PermutationId; }

    void SetName(const jName& InName) { Name = InName; Hash = 0; }
    void SetShaderFilepath(const jName& InShaderFilepath) { ShaderFilepath = InShaderFilepath; Hash = 0; }
    void SetPreProcessors(const jName& InPreProcessors) { PreProcessors = InPreProcessors; Hash = 0; }
    void SetEntryPoint(const jName& InEntryPoint) { EntryPoint = InEntryPoint; Hash = 0; }
    void SetShaderType(const EShaderAccessStageFlag InShaderType) { ShaderType = InShaderType; Hash = 0; }
    void SetPermutationId(const uint32 InPermutationId) { PermutationId = InPermutationId; Hash = 0; }

private:
	jName Name;
    jName ShaderFilepath;
    jName PreProcessors;
    jName EntryPoint;
    EShaderAccessStageFlag ShaderType = (EShaderAccessStageFlag)0;
    uint32 PermutationId = 0;
};

struct jCompiledShader
{
	virtual ~jCompiledShader() {}
};

struct jShader : public std::enable_shared_from_this<jShader>
{
	jShader()
	{}
    jShader(const jShaderInfo& shaderInfo)
        : ShaderInfo(shaderInfo)
    { }
	virtual ~jShader();

	static void UpdateShaders();

    void UpdateShader();
    virtual void Initialize();

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
    static jShaderInfo GShaderInfo; \
    static ShaderClass* CreateShader(const ShaderClass::ShaderPermutation& InPermutation); \
    using jShader::jShader; \
    virtual void SetPermutationId(int32 InPermutaitonId) override { PermutationVariable.SetFromPermutationId(InPermutaitonId); } \
    virtual int32 GetPermutationId() const override { return PermutationVariable.GetPermutationId(); } \
    virtual int32 GetPermutationCount() const override { return PermutationVariable.GetPermutationCount(); } \
    virtual void GetPermutationDefines(std::string& OutResult) const { PermutationVariable.GetPermutationDefines(OutResult); }

#define IMPLEMENT_SHADER_WITH_PERMUTATION(ShaderClass, Name, Filepath, Preprocessor, EntryName, ShaderAccesssStageFlag) \
jShaderInfo ShaderClass::GShaderInfo( \
    jNameStatic(Name), \
    jNameStatic(Filepath), \
    jNameStatic(Preprocessor), \
    jNameStatic(EntryName), \
    ShaderAccesssStageFlag \
); \
ShaderClass* ShaderClass::CreateShader(const ShaderClass::ShaderPermutation& InPermutation) \
{ \
    jShaderInfo TempShaderInfo = GShaderInfo; \
    TempShaderInfo.SetPermutationId(InPermutation.GetPermutationId()); \
    ShaderClass* Shader = g_rhi->CreateShader<ShaderClass>(TempShaderInfo); \
    Shader->Permutation = InPermutation; \
    return Shader; \
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
    DECLARE_DEFINE(USE_ALBEDO_TEXTURE, 0, 1);

    using ShaderPermutation = jPermutation<USE_VERTEX_COLOR, USE_ALBEDO_TEXTURE>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderGBufferVertexShader, Permutation)
};

struct jShaderGBufferPixelShader : public jShader
{
    DECLARE_DEFINE(USE_VERTEX_COLOR, 0, 1);
    DECLARE_DEFINE(USE_ALBEDO_TEXTURE, 0, 1);
    DECLARE_DEFINE(USE_VARIABLE_SHADING_RATE, 0, 1);
    DECLARE_DEFINE(USE_PBR, 0, 1);
    
    using ShaderPermutation = jPermutation<USE_VERTEX_COLOR, USE_ALBEDO_TEXTURE, USE_VARIABLE_SHADING_RATE, USE_PBR>;
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

