#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Shader/jShader.h"
#include "RHI/jShaderBindingLayout.h"
#include "RHI/jRHI.h"

struct Vector;
struct Vector2;
struct Vector4;
struct Vector2i;
struct Vector3i;
struct Vector4i;
struct Matrix;
struct Matrix3;

struct jShaderParameterTexture2D
{
    jTexture* Texture = nullptr;
    const jSamplerStateInfo* SamplerState = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterTexture2DSRV
{
    jTexture* Texture = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterTextureCube
{
    jTexture* Texture = nullptr;
    const jSamplerStateInfo* SamplerState = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterTextureCubeSRV
{
    jTexture* Texture = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterTexture2DComparison
{
    jTexture* Texture = nullptr;
    const jSamplerStateInfo* SamplerState = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterSubpassInput
{
    jTexture* Texture = nullptr;
};

struct jShaderParameterSubpassInputFloat
{
    jTexture* Texture = nullptr;
};

struct jShaderParameterTextureCubeComparison
{
    jTexture* Texture = nullptr;
    const jSamplerStateInfo* SamplerState = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterSampler
{
    const jSamplerStateInfo* SamplerState = nullptr;
};

struct jShaderParameterAccelerationStructure
{
    jBuffer* Buffer = nullptr;
};

struct jShaderParameterRWTexture2D
{
    jTexture* Texture = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterRWTexture2DFloat2
{
    jTexture* Texture = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterRWTexture2DFloat
{
    jTexture* Texture = nullptr;
    int32 MipLevel = 0;
};

struct jShaderParameterRWTexture2DArray
{
    jTexture* Texture = nullptr;
    int32 MipLevel = 0;
};

template <typename T>
struct jShaderParameterUniformBuffer
{
    std::shared_ptr<IUniformBufferBlock> Buffer = nullptr;
};

template <typename T>
struct jShaderParameterStructuredBuffer
{
    jBuffer* Buffer = nullptr;
};

template <typename T>
struct jShaderParameterRWStructuredBuffer
{
    jBuffer* Buffer = nullptr;
};

template <typename T>
struct jShaderParameterBindlessBuffer
{
    std::vector<const jBuffer*> Buffers;
};

template <typename T>
struct jShaderParameterBindlessStructuredBuffer
{
    std::vector<const jBuffer*> Buffers;
};

struct jShaderParameterBindlessByteAddressBuffer
{
    std::vector<const jBuffer*> Buffers;
};

template <typename T>
struct jShaderParameterBindlessUniformBuffer
{
    std::vector<const IUniformBufferBlock*> Buffers;
};

struct jShaderParameterBindlessTexture2DSRV
{
    std::vector<jTextureResourceBindless::jTextureBindData> Textures;
};

struct jShaderParameterBindlessTextureCubeSRV
{
    std::vector<jTextureResourceBindless::jTextureBindData> Textures;
};

struct jShaderParameterBindlessSampler
{
    std::vector<const jSamplerStateInfo*> SamplerStates;
};

template <typename T>
inline constexpr bool jShaderParameterDependentFalse_v = false;

struct jShaderUniformBufferFieldMeta
{
    using AppendTypeDeclarationFunc = void(*)(std::string&);

    const char* Name = nullptr;
    const char* HLSLTypeName = nullptr;
    uint32 ArrayCount = 1;
    AppendTypeDeclarationFunc AppendTypeDeclaration = nullptr;
    bool IsArray = false;
};

template <typename T>
struct TShaderUniformBufferFieldHLSLTypeInfo
{
    static constexpr const char* GetTypeName()
    {
        if constexpr (requires
        {
            T::GetShaderUniformBufferTypeName();
            T::GetShaderUniformBufferFieldMembers();
        })
        {
            return T::GetShaderUniformBufferTypeName();
        }
        else
        {
            static_assert(jShaderParameterDependentFalse_v<T>, "Provide TShaderUniformBufferFieldHLSLTypeInfo<T> specialization for uniform buffer fields.");
            return "";
        }
    }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<float>
{
    static constexpr const char* GetTypeName() { return "float"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<int32>
{
    static constexpr const char* GetTypeName() { return "int"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<uint32>
{
    static constexpr const char* GetTypeName() { return "uint"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Vector2>
{
    static constexpr const char* GetTypeName() { return "float2"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Vector>
{
    static constexpr const char* GetTypeName() { return "float3"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Vector4>
{
    static constexpr const char* GetTypeName() { return "float4"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Vector2i>
{
    static constexpr const char* GetTypeName() { return "int2"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Vector3i>
{
    static constexpr const char* GetTypeName() { return "int3"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Vector4i>
{
    static constexpr const char* GetTypeName() { return "int4"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Matrix>
{
    static constexpr const char* GetTypeName() { return "float4x4"; }
};

template <>
struct TShaderUniformBufferFieldHLSLTypeInfo<Matrix3>
{
    static constexpr const char* GetTypeName() { return "float3x3"; }
};

template <typename T, typename = void>
struct THasGeneratedShaderUniformBufferTypeInfo : std::false_type
{};

template <typename T>
struct THasGeneratedShaderUniformBufferTypeInfo<T, std::void_t<
    decltype(T::GetShaderUniformBufferTypeName()),
    decltype(T::GetShaderUniformBufferFieldMembers())>> : std::true_type
{};

template <typename T>
struct TShaderUniformBufferFieldAlignmentInfo
{
    static constexpr size_t Value()
    {
        if constexpr (THasGeneratedShaderUniformBufferTypeInfo<T>::value)
        {
            static_assert((sizeof(T) % 16) == 0, "Generated uniform-buffer field structs must have 16-byte sized layout.");
            return 16;
        }
        else
        {
            static_assert(jShaderParameterDependentFalse_v<T>, "Provide TShaderUniformBufferFieldAlignmentInfo<T> specialization for uniform buffer fields.");
            return 0;
        }
    }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<float>
{
    static constexpr size_t Value() { return 4; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<int32>
{
    static constexpr size_t Value() { return 4; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<uint32>
{
    static constexpr size_t Value() { return 4; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Vector2>
{
    static constexpr size_t Value() { return 8; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Vector>
{
    static constexpr size_t Value() { return 16; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Vector4>
{
    static constexpr size_t Value() { return 16; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Vector2i>
{
    static constexpr size_t Value() { return 8; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Vector3i>
{
    static constexpr size_t Value() { return 16; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Vector4i>
{
    static constexpr size_t Value() { return 16; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Matrix>
{
    static constexpr size_t Value() { return 16; }
};

template <>
struct TShaderUniformBufferFieldAlignmentInfo<Matrix3>
{
    static constexpr size_t Value() { return 16; }
};

template <typename T>
struct TShaderUniformBufferArrayFieldInfo
{
    static constexpr bool IsSupported()
    {
        return (TShaderUniformBufferFieldAlignmentInfo<T>::Value() == 16) && ((sizeof(T) % 16) == 0);
    }
};

namespace jShaderParameterDetail
{
    template <typename... TArgs>
    void AppendLine(std::string& Out, TArgs&&... InArgs);
}

template <typename T>
struct TShaderParameterHLSLTypeInfo
{
    static constexpr const char* GetTypeName()
    {
        if constexpr (THasGeneratedShaderUniformBufferTypeInfo<T>::value)
        {
            return T::GetShaderUniformBufferTypeName();
        }
        else
        {
            static_assert(jShaderParameterDependentFalse_v<T>, "Provide TShaderParameterHLSLTypeInfo<T> specialization for uniform buffer parameter types.");
            return "";
        }
    }

        static void AppendTypeDeclaration(std::string& Out)
        {
            if constexpr (THasGeneratedShaderUniformBufferTypeInfo<T>::value)
            {
                for (const jShaderUniformBufferFieldMeta& Meta : T::GetShaderUniformBufferFieldMembers())
                {
                    if (Meta.AppendTypeDeclaration)
                        Meta.AppendTypeDeclaration(Out);
                }

                jShaderParameterDetail::AppendLine(Out, "#ifndef JENGINE_GENERATED_TYPE_", GetTypeName());
                jShaderParameterDetail::AppendLine(Out, "#define JENGINE_GENERATED_TYPE_", GetTypeName(), " 1");
                jShaderParameterDetail::AppendLine(Out, "struct ", GetTypeName());
                jShaderParameterDetail::AppendLine(Out, "{");
                for (const jShaderUniformBufferFieldMeta& Meta : T::GetShaderUniformBufferFieldMembers())
                {
                    if (Meta.IsArray)
                        jShaderParameterDetail::AppendLine(Out, "    ", Meta.HLSLTypeName, " ", Meta.Name, "[", Meta.ArrayCount, "];");
                    else
                        jShaderParameterDetail::AppendLine(Out, "    ", Meta.HLSLTypeName, " ", Meta.Name, ";");
                }
                jShaderParameterDetail::AppendLine(Out, "};");
                jShaderParameterDetail::AppendLine(Out, "#endif");
            }
            else
            {
                static_assert(jShaderParameterDependentFalse_v<T>, "Provide TShaderParameterHLSLTypeInfo<T> specialization for uniform buffer parameter types.");
        }
    }
};

template <>
struct TShaderParameterHLSLTypeInfo<float>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<float>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<int32>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<int32>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<uint32>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<uint32>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Vector2>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Vector2>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Vector>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Vector>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Vector4>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Vector4>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Vector2i>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Vector2i>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Vector3i>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Vector3i>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Vector4i>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Vector4i>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Matrix>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Matrix>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<Matrix3>
{
    static constexpr const char* GetTypeName() { return TShaderUniformBufferFieldHLSLTypeInfo<Matrix3>::GetTypeName(); }
    static void AppendTypeDeclaration(std::string&) {}
};

struct jShaderParameterBindingCursor
{
    uint32 BindingIndex = 0;
};

struct jShaderParameterMemberMeta
{
    using AppendTypeDeclarationFunc = void(*)(std::string&);
    using AppendHLSLFunc = void(*)(std::string&, uint32, jShaderParameterBindingCursor&);
    using BuildBindingFunc = void(*)(const void*, EShaderAccessStageFlag, jShaderBindingArray&, jShaderBindingResourceInlineAllocator&, jShaderParameterBindingCursor&);

    const char* Name = nullptr;
    const char* HLSLTypeName = nullptr;
    AppendTypeDeclarationFunc AppendTypeDeclaration = nullptr;
    AppendHLSLFunc AppendHLSL = nullptr;
    BuildBindingFunc BuildBinding = nullptr;
};

struct jShaderBindlessMemberMeta
{
    using AppendTypeDeclarationFunc = void(*)(std::string&);
    using AppendHLSLFunc = void(*)(std::string&, const char*, uint32);
    using BuildBindingFunc = void(*)(const void*, EShaderAccessStageFlag, jShaderBindingArray&, jShaderBindingResourceInlineAllocator&);

    const char* Name = nullptr;
    const char* HLSLTypeName = nullptr;
    uint32 Space = 0;
    AppendTypeDeclarationFunc AppendTypeDeclaration = nullptr;
    AppendHLSLFunc AppendHLSL = nullptr;
    BuildBindingFunc BuildBinding = nullptr;
};

template <typename SetType, int Line>
struct TShaderParameterTag
{};

template <typename SetType, int Line>
struct TShaderBindlessTag
{};

template <typename BufferType, int Line>
struct TShaderUniformBufferFieldTag
{};

namespace jShaderParameterDetail
{
    inline void AppendString(std::string& Out, const char* InValue)
    {
        Out += InValue;
    }

    inline void AppendString(std::string& Out, std::string_view InValue)
    {
        Out.append(InValue.data(), InValue.size());
    }

    inline void AppendString(std::string& Out, const std::string& InValue)
    {
        Out += InValue;
    }

    template <typename T, std::enable_if_t<std::is_integral_v<std::decay_t<T>>, int> = 0>
    inline void AppendString(std::string& Out, T InValue)
    {
        Out += std::to_string(InValue);
    }

    inline void AppendString(std::string&) {}

    template <typename T, typename... TArgs>
    inline void AppendString(std::string& Out, T&& InValue, TArgs&&... InArgs)
    {
        AppendString(Out, std::forward<T>(InValue));
        if constexpr (sizeof...(InArgs) > 0)
            AppendString(Out, std::forward<TArgs>(InArgs)...);
    }

    template <typename... TArgs>
    inline void AppendLine(std::string& Out, TArgs&&... InArgs)
    {
        AppendString(Out, std::forward<TArgs>(InArgs)...);
        Out += "\r\n";
    }

    template <typename ParameterType>
    struct TShaderParameterTypeHandler;

    template <typename ParameterType>
    struct TShaderBindlessTypeHandler;

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterTexture2D>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "Texture2D ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            AppendLine(Out, "SamplerState ", InName, "Sampler : register(s", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterTexture2D& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, InParameter.SamplerState, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterTexture2DSRV>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "Texture2D ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterTexture2DSRV& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterTextureCube>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "TextureCube<float4> ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            AppendLine(Out, "SamplerState ", InName, "Sampler : register(s", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterTextureCube& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, InParameter.SamplerState, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterTextureCubeSRV>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "TextureCube<float4> ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterTextureCubeSRV& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterTexture2DComparison>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "Texture2D ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            AppendLine(Out, "SamplerComparisonState ", InName, "Sampler : register(s", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterTexture2DComparison& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, InParameter.SamplerState, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterSubpassInput>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "[[vk::input_attachment_index(", InOutCursor.BindingIndex, ")]] [[vk::binding(", InOutCursor.BindingIndex, ", ", InSpace, ")]] SubpassInput ", InName, ";");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterSubpassInput& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::SUBPASS_INPUT_ATTACHMENT, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterSubpassInputFloat>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "[[vk::input_attachment_index(", InOutCursor.BindingIndex, ")]] [[vk::binding(", InOutCursor.BindingIndex, ", ", InSpace, ")]] SubpassInput<float> ", InName, ";");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterSubpassInputFloat& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::SUBPASS_INPUT_ATTACHMENT, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterTextureCubeComparison>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "TextureCube<float4> ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            AppendLine(Out, "SamplerComparisonState ", InName, "Sampler : register(s", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterTextureCubeComparison& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, InParameter.SamplerState, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterSampler>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "SamplerState ", InName, " : register(s", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterSampler& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.SamplerState);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::SAMPLER, InShaderAccessStageFlags
                , OutAllocator.Alloc<jSamplerResource>(InParameter.SamplerState)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterAccelerationStructure>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "RaytracingAccelerationStructure ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterAccelerationStructure& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Buffer);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::ACCELERATION_STRUCTURE_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jBufferResource>(InParameter.Buffer), true));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterRWTexture2D>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "RWTexture2D<float4> ", InName, " : register(u", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterRWTexture2D& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_UAV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterRWTexture2DFloat2>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "RWTexture2D<float2> ", InName, " : register(u", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterRWTexture2DFloat2& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_UAV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterRWTexture2DFloat>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "RWTexture2D<float> ", InName, " : register(u", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterRWTexture2DFloat& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_UAV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr, InParameter.MipLevel)));
        }
    };

    template <>
    struct TShaderParameterTypeHandler<jShaderParameterRWTexture2DArray>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "RWTexture2DArray<float4> ", InName, " : register(u", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterRWTexture2DArray& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Texture);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::TEXTURE_UAV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResource>(InParameter.Texture, nullptr, InParameter.MipLevel)));
        }
    };

    template <typename T>
    struct TShaderParameterTypeHandler<jShaderParameterUniformBuffer<T>>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return TShaderParameterHLSLTypeInfo<T>::GetTypeName();
        }

        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderParameterHLSLTypeInfo<T>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "cbuffer ", InName, " : register(b", InOutCursor.BindingIndex, ", space", InSpace, ")");
            AppendLine(Out, "{");
            AppendLine(Out, "    ", GetHLSLTypeName(), " ", InName, ";");
            AppendLine(Out, "};");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterUniformBuffer<T>& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Buffer);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, InShaderAccessStageFlags
                , OutAllocator.Alloc<jUniformBufferResource>(InParameter.Buffer.get()), true));
        }
    };

    template <typename T>
    struct TShaderParameterTypeHandler<jShaderParameterStructuredBuffer<T>>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return TShaderParameterHLSLTypeInfo<T>::GetTypeName();
        }

        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderParameterHLSLTypeInfo<T>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "StructuredBuffer<", GetHLSLTypeName(), "> ", InName, " : register(t", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterStructuredBuffer<T>& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Buffer);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::BUFFER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jBufferResource>(InParameter.Buffer)));
        }
    };

    template <typename T>
    struct TShaderParameterTypeHandler<jShaderParameterRWStructuredBuffer<T>>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return TShaderParameterHLSLTypeInfo<T>::GetTypeName();
        }

        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderParameterHLSLTypeInfo<T>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            AppendLine(Out, "RWStructuredBuffer<", GetHLSLTypeName(), "> ", InName, " : register(u", InOutCursor.BindingIndex, ", space", InSpace, ");");
            ++InOutCursor.BindingIndex;
        }

        static void BuildBinding(const jShaderParameterRWStructuredBuffer<T>& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            check(InParameter.Buffer);
            OutBindings.Add(jShaderBinding::Create((int32)InOutCursor.BindingIndex++, 1, EShaderBindingType::BUFFER_UAV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jBufferResource>(InParameter.Buffer)));
        }
    };

    template <typename T>
    struct TShaderBindlessTypeHandler<jShaderParameterBindlessBuffer<T>>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return TShaderParameterHLSLTypeInfo<T>::GetTypeName();
        }

        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderParameterHLSLTypeInfo<T>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            AppendLine(Out, "Buffer<", GetHLSLTypeName(), "> ", InName, "[] : register(t0, space", InSpace, ");");
        }

        static void BuildBinding(const jShaderParameterBindlessBuffer<T>& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            OutBindings.Add(jShaderBinding::CreateBindless(0, (int32)InParameter.Buffers.size(), EShaderBindingType::BUFFER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jBufferResourceBindless>(InParameter.Buffers)));
        }
    };

    template <typename T>
    struct TShaderBindlessTypeHandler<jShaderParameterBindlessStructuredBuffer<T>>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return TShaderParameterHLSLTypeInfo<T>::GetTypeName();
        }

        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderParameterHLSLTypeInfo<T>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            AppendLine(Out, "StructuredBuffer<", GetHLSLTypeName(), "> ", InName, "[] : register(t0, space", InSpace, ");");
        }

        static void BuildBinding(const jShaderParameterBindlessStructuredBuffer<T>& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            OutBindings.Add(jShaderBinding::CreateBindless(0, (int32)InParameter.Buffers.size(), EShaderBindingType::BUFFER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jBufferResourceBindless>(InParameter.Buffers)));
        }
    };

    template <>
    struct TShaderBindlessTypeHandler<jShaderParameterBindlessByteAddressBuffer>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            AppendLine(Out, "ByteAddressBuffer ", InName, "[] : register(t0, space", InSpace, ");");
        }

        static void BuildBinding(const jShaderParameterBindlessByteAddressBuffer& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            OutBindings.Add(jShaderBinding::CreateBindless(0, (int32)InParameter.Buffers.size(), EShaderBindingType::BUFFER_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jBufferResourceBindless>(InParameter.Buffers)));
        }
    };

    template <typename T>
    struct TShaderBindlessTypeHandler<jShaderParameterBindlessUniformBuffer<T>>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return TShaderParameterHLSLTypeInfo<T>::GetTypeName();
        }

        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderParameterHLSLTypeInfo<T>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            AppendLine(Out, "ConstantBuffer<", GetHLSLTypeName(), "> ", InName, "[] : register(b0, space", InSpace, ");");
        }

        static void BuildBinding(const jShaderParameterBindlessUniformBuffer<T>& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            OutBindings.Add(jShaderBinding::CreateBindless(0, (int32)InParameter.Buffers.size(), EShaderBindingType::UNIFORMBUFFER, InShaderAccessStageFlags
                , OutAllocator.Alloc<jUniformBufferResourceBindless>(InParameter.Buffers)));
        }
    };

    template <>
    struct TShaderBindlessTypeHandler<jShaderParameterBindlessTexture2DSRV>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            AppendLine(Out, "Texture2D ", InName, "[] : register(t0, space", InSpace, ");");
        }

        static void BuildBinding(const jShaderParameterBindlessTexture2DSRV& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            OutBindings.Add(jShaderBinding::CreateBindless(0, (int32)InParameter.Textures.size(), EShaderBindingType::TEXTURE_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResourceBindless>(InParameter.Textures)));
        }
    };

    template <>
    struct TShaderBindlessTypeHandler<jShaderParameterBindlessTextureCubeSRV>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            AppendLine(Out, "TextureCube<float4> ", InName, "[] : register(t0, space", InSpace, ");");
        }

        static void BuildBinding(const jShaderParameterBindlessTextureCubeSRV& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            OutBindings.Add(jShaderBinding::CreateBindless(0, (int32)InParameter.Textures.size(), EShaderBindingType::TEXTURE_SRV, InShaderAccessStageFlags
                , OutAllocator.Alloc<jTextureResourceBindless>(InParameter.Textures)));
        }
    };

    template <>
    struct TShaderBindlessTypeHandler<jShaderParameterBindlessSampler>
    {
        static constexpr const char* GetHLSLTypeName()
        {
            return nullptr;
        }

        static void AppendTypeDeclaration(std::string&)
        {
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            AppendLine(Out, "SamplerState ", InName, "[] : register(s0, space", InSpace, ");");
        }

        static void BuildBinding(const jShaderParameterBindlessSampler& InParameter, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            OutBindings.Add(jShaderBinding::CreateBindless(0, (int32)InParameter.SamplerStates.size(), EShaderBindingType::SAMPLER, InShaderAccessStageFlags
                , OutAllocator.Alloc<jSamplerResourceBindless>(InParameter.SamplerStates)));
        }
    };

    template <typename SetType, typename ParameterType, ParameterType SetType::* MemberPtr, typename NameProvider>
    struct TShaderParameterMemberEntry
    {
        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderParameterTypeHandler<ParameterType>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, uint32 InSpace, jShaderParameterBindingCursor& InOutCursor)
        {
            TShaderParameterTypeHandler<ParameterType>::AppendHLSL(Out, NameProvider::Get(), InSpace, InOutCursor);
        }

        static void BuildBinding(const void* InInstance, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator, jShaderParameterBindingCursor& InOutCursor)
        {
            const SetType& Instance = *reinterpret_cast<const SetType*>(InInstance);
            TShaderParameterTypeHandler<ParameterType>::BuildBinding(Instance.*MemberPtr, InShaderAccessStageFlags, OutBindings, OutAllocator, InOutCursor);
        }

        static constexpr jShaderParameterMemberMeta GetMeta()
        {
            return {
                NameProvider::Get(),
                TShaderParameterTypeHandler<ParameterType>::GetHLSLTypeName(),
                &AppendTypeDeclaration,
                &AppendHLSL,
                &BuildBinding
            };
        }
    };

    template <typename SetType, int Line>
    concept HasShaderParameterMeta = requires
    {
        SetType::__jShaderParameterMeta(TShaderParameterTag<SetType, Line>{});
    };

    template <typename SetType, typename ParameterType, ParameterType SetType::* MemberPtr, typename NameProvider, uint32 SpaceValue>
    struct TShaderBindlessMemberEntry
    {
        static void AppendTypeDeclaration(std::string& Out)
        {
            TShaderBindlessTypeHandler<ParameterType>::AppendTypeDeclaration(Out);
        }

        static void AppendHLSL(std::string& Out, const char* InName, uint32 InSpace)
        {
            TShaderBindlessTypeHandler<ParameterType>::AppendHLSL(Out, InName, InSpace);
        }

        static void BuildBinding(const void* InInstance, EShaderAccessStageFlag InShaderAccessStageFlags
            , jShaderBindingArray& OutBindings, jShaderBindingResourceInlineAllocator& OutAllocator)
        {
            const SetType& Instance = *reinterpret_cast<const SetType*>(InInstance);
            TShaderBindlessTypeHandler<ParameterType>::BuildBinding(Instance.*MemberPtr, InShaderAccessStageFlags, OutBindings, OutAllocator);
        }

        static constexpr jShaderBindlessMemberMeta GetMeta()
        {
            return {
                NameProvider::Get(),
                TShaderBindlessTypeHandler<ParameterType>::GetHLSLTypeName(),
                SpaceValue,
                &AppendTypeDeclaration,
                &AppendHLSL,
                &BuildBinding
            };
        }
    };

    template <typename SetType, int Line>
    concept HasShaderBindlessMeta = requires
    {
        SetType::__jShaderBindlessMeta(TShaderBindlessTag<SetType, Line>{});
    };

    template <typename SetType, int Line, int EndLine>
    constexpr size_t CountShaderBindlessMembers()
    {
        if constexpr (Line >= EndLine)
        {
            return 0;
        }
        else if constexpr (HasShaderBindlessMeta<SetType, Line>)
        {
            return 1 + CountShaderBindlessMembers<SetType, Line + 1, EndLine>();
        }
        else
        {
            return CountShaderBindlessMembers<SetType, Line + 1, EndLine>();
        }
    }

    template <typename SetType, int Line, int EndLine, size_t N>
    constexpr void FillShaderBindlessMembers(std::array<jShaderBindlessMemberMeta, N>& OutMembers, size_t& InOutIndex)
    {
        if constexpr (Line < EndLine)
        {
            if constexpr (HasShaderBindlessMeta<SetType, Line>)
            {
                OutMembers[InOutIndex++] = SetType::__jShaderBindlessMeta(TShaderBindlessTag<SetType, Line>{});
            }
            FillShaderBindlessMembers<SetType, Line + 1, EndLine>(OutMembers, InOutIndex);
        }
    }

    template <typename SetType, int BeginLine, int EndLine>
    constexpr auto CollectShaderBindlessMembers()
    {
        constexpr size_t Count = CountShaderBindlessMembers<SetType, BeginLine, EndLine>();
        std::array<jShaderBindlessMemberMeta, Count> Result{};
        size_t Index = 0;
        FillShaderBindlessMembers<SetType, BeginLine, EndLine>(Result, Index);
        return Result;
    }

    template <typename SetType, int Line, int EndLine>
    constexpr size_t CountShaderParameterMembers()
    {
        if constexpr (Line >= EndLine)
        {
            return 0;
        }
        else if constexpr (HasShaderParameterMeta<SetType, Line>)
        {
            return 1 + CountShaderParameterMembers<SetType, Line + 1, EndLine>();
        }
        else
        {
            return CountShaderParameterMembers<SetType, Line + 1, EndLine>();
        }
    }

    template <typename SetType, int Line, int EndLine, size_t N>
    constexpr void FillShaderParameterMembers(std::array<jShaderParameterMemberMeta, N>& OutMembers, size_t& InOutIndex)
    {
        if constexpr (Line < EndLine)
        {
            if constexpr (HasShaderParameterMeta<SetType, Line>)
            {
                OutMembers[InOutIndex++] = SetType::__jShaderParameterMeta(TShaderParameterTag<SetType, Line>{});
            }
            FillShaderParameterMembers<SetType, Line + 1, EndLine>(OutMembers, InOutIndex);
        }
    }

    template <typename SetType, int BeginLine, int EndLine>
    constexpr auto CollectShaderParameterMembers()
    {
        constexpr size_t Count = CountShaderParameterMembers<SetType, BeginLine, EndLine>();
        std::array<jShaderParameterMemberMeta, Count> Result{};
        size_t Index = 0;
        FillShaderParameterMembers<SetType, BeginLine, EndLine>(Result, Index);
        return Result;
    }

    template <typename BufferType, typename FieldType, FieldType BufferType::* MemberPtr, typename NameProvider>
    struct TShaderUniformBufferFieldEntry
    {
        static constexpr jShaderUniformBufferFieldMeta GetMeta()
        {
            return { NameProvider::Get(), TShaderUniformBufferFieldHLSLTypeInfo<FieldType>::GetTypeName(), 1, &TShaderParameterHLSLTypeInfo<FieldType>::AppendTypeDeclaration };
        }
    };

    template <typename BufferType, int Line>
    concept HasShaderUniformBufferFieldMeta = requires
    {
        BufferType::__jShaderUniformBufferFieldMeta(TShaderUniformBufferFieldTag<BufferType, Line>{});
    };

    template <typename BufferType, int Line, int EndLine>
    constexpr size_t CountShaderUniformBufferFieldMembers()
    {
        if constexpr (Line >= EndLine)
        {
            return 0;
        }
        else if constexpr (HasShaderUniformBufferFieldMeta<BufferType, Line>)
        {
            return 1 + CountShaderUniformBufferFieldMembers<BufferType, Line + 1, EndLine>();
        }
        else
        {
            return CountShaderUniformBufferFieldMembers<BufferType, Line + 1, EndLine>();
        }
    }

    template <typename BufferType, int Line, int EndLine, size_t N>
    constexpr void FillShaderUniformBufferFieldMembers(std::array<jShaderUniformBufferFieldMeta, N>& OutMembers, size_t& InOutIndex)
    {
        if constexpr (Line < EndLine)
        {
            if constexpr (HasShaderUniformBufferFieldMeta<BufferType, Line>)
            {
                OutMembers[InOutIndex++] = BufferType::__jShaderUniformBufferFieldMeta(TShaderUniformBufferFieldTag<BufferType, Line>{});
            }
            FillShaderUniformBufferFieldMembers<BufferType, Line + 1, EndLine>(OutMembers, InOutIndex);
        }
    }

    template <typename BufferType, int BeginLine, int EndLine>
    constexpr auto CollectShaderUniformBufferFieldMembers()
    {
        constexpr size_t Count = CountShaderUniformBufferFieldMembers<BufferType, BeginLine, EndLine>();
        std::array<jShaderUniformBufferFieldMeta, Count> Result{};
        size_t Index = 0;
        FillShaderUniformBufferFieldMembers<BufferType, BeginLine, EndLine>(Result, Index);
        return Result;
    }
}

namespace jShaderParameterSet
{
    template <typename TParameterSet>
    std::string GenerateHLSL(uint32 InSpace)
    {
        std::string Result;
        std::unordered_set<std::string_view> GeneratedTypeDeclarations;
        for (const jShaderParameterMemberMeta& Meta : TParameterSet::GetShaderParameterMembers())
        {
            if (!Meta.HLSLTypeName || !Meta.AppendTypeDeclaration)
                continue;

            if (GeneratedTypeDeclarations.insert(Meta.HLSLTypeName).second)
            {
                Meta.AppendTypeDeclaration(Result);
                if (!Result.empty() && Result.back() != '\n')
                    Result += "\r\n";
                Result += "\r\n";
            }
        }

        jShaderParameterBindingCursor BindingCursor;
        for (const jShaderParameterMemberMeta& Meta : TParameterSet::GetShaderParameterMembers())
        {
            check(Meta.AppendHLSL);
            Meta.AppendHLSL(Result, InSpace, BindingCursor);
        }
        return Result;
    }

    template <typename TParameterSet>
    void AppendToShaderInfo(jShaderInfo& InOutShaderInfo, uint32 InSpace)
    {
        std::string InjectedShaderText;
        if (const char* Existing = InOutShaderInfo.GetInjectedShaderText().ToStr())
            InjectedShaderText = Existing;

        if (!InjectedShaderText.empty() && InjectedShaderText.back() != '\n')
            InjectedShaderText += "\r\n";
        InjectedShaderText += GenerateHLSL<TParameterSet>(InSpace);
        InOutShaderInfo.SetInjectedShaderText(jName(InjectedShaderText));
    }

    template <typename TParameterSet>
    void BuildShaderBindings(const TParameterSet& InParameters, EShaderAccessStageFlag InShaderAccessStageFlags
        , jShaderBindingArray& OutShaderBindings, jShaderBindingResourceInlineAllocator& OutResourceInlineAllocator)
    {
        jShaderParameterBindingCursor BindingCursor;
        for (const jShaderParameterMemberMeta& Meta : TParameterSet::GetShaderParameterMembers())
        {
            check(Meta.BuildBinding);
            Meta.BuildBinding(&InParameters, InShaderAccessStageFlags, OutShaderBindings, OutResourceInlineAllocator, BindingCursor);
        }
    }

    template <typename TParameterSet>
    std::shared_ptr<jShaderBindingInstance> CreateShaderBindingInstance(const TParameterSet& InParameters
        , EShaderAccessStageFlag InShaderAccessStageFlags, jShaderBindingInstanceType InType)
    {
        jShaderBindingArray ShaderBindings;
        jShaderBindingResourceInlineAllocator ResourceInlineAllocator;
        BuildShaderBindings(InParameters, InShaderAccessStageFlags, ShaderBindings, ResourceInlineAllocator);
        return g_rhi->CreateShaderBindingInstance(ShaderBindings, InType);
    }
}

namespace jShaderBindlessSet
{
    template <typename TBindlessSet>
    uint32 GetMinSpace()
    {
        bool HasSpace = false;
        uint32 MinSpace = 0;
        for (const jShaderBindlessMemberMeta& Meta : TBindlessSet::GetShaderBindlessMembers())
        {
            if (!HasSpace)
            {
                MinSpace = Meta.Space;
                HasSpace = true;
            }
            else if (Meta.Space < MinSpace)
            {
                MinSpace = Meta.Space;
            }
        }
        check(HasSpace);
        return MinSpace;
    }

    template <typename TBindlessSet>
    uint32 GetMaxSpace()
    {
        bool HasSpace = false;
        uint32 MaxSpace = 0;
        for (const jShaderBindlessMemberMeta& Meta : TBindlessSet::GetShaderBindlessMembers())
        {
            if (!HasSpace)
            {
                MaxSpace = Meta.Space;
                HasSpace = true;
            }
            else if (Meta.Space > MaxSpace)
            {
                MaxSpace = Meta.Space;
            }
        }
        check(HasSpace);
        return MaxSpace;
    }

    template <typename TBindlessSet>
    uint32 GetNextSpaceAfter()
    {
        return GetMaxSpace<TBindlessSet>() + 1u;
    }

    template <typename TBindlessSet>
    std::string GenerateHLSL()
    {
        std::string Result;
        std::unordered_set<std::string_view> GeneratedTypeDeclarations;
        for (const jShaderBindlessMemberMeta& Meta : TBindlessSet::GetShaderBindlessMembers())
        {
            if (!Meta.HLSLTypeName || !Meta.AppendTypeDeclaration)
                continue;

            if (GeneratedTypeDeclarations.insert(Meta.HLSLTypeName).second)
            {
                Meta.AppendTypeDeclaration(Result);
                if (!Result.empty() && Result.back() != '\n')
                    Result += "\r\n";
                Result += "\r\n";
            }
        }

        for (const jShaderBindlessMemberMeta& Meta : TBindlessSet::GetShaderBindlessMembers())
        {
            check(Meta.AppendHLSL);
            Meta.AppendHLSL(Result, Meta.Name, Meta.Space);
        }
        return Result;
    }

    template <typename TBindlessSet>
    void AppendToShaderInfo(jShaderInfo& InOutShaderInfo)
    {
        std::string InjectedShaderText;
        if (const char* Existing = InOutShaderInfo.GetInjectedShaderText().ToStr())
            InjectedShaderText = Existing;

        if (!InjectedShaderText.empty() && InjectedShaderText.back() != '\n')
            InjectedShaderText += "\r\n";
        InjectedShaderText += GenerateHLSL<TBindlessSet>();
        InOutShaderInfo.SetInjectedShaderText(jName(InjectedShaderText));
    }

    template <typename TBindlessSet>
    std::vector<std::shared_ptr<jShaderBindingInstance>> CreateShaderBindingInstances(const TBindlessSet& InParameters
        , EShaderAccessStageFlag InShaderAccessStageFlags, jShaderBindingInstanceType InType)
    {
        std::vector<std::shared_ptr<jShaderBindingInstance>> Result;
        Result.reserve(TBindlessSet::GetShaderBindlessMembers().size());
        for (const jShaderBindlessMemberMeta& Meta : TBindlessSet::GetShaderBindlessMembers())
        {
            jShaderBindingArray ShaderBindings;
            jShaderBindingResourceInlineAllocator ResourceInlineAllocator;
            check(Meta.BuildBinding);
            Meta.BuildBinding(&InParameters, InShaderAccessStageFlags, ShaderBindings, ResourceInlineAllocator);
            Result.push_back(g_rhi->CreateShaderBindingInstance(ShaderBindings, InType));
        }
        return Result;
    }
}

template <typename TParameterSet>
inline void AppendShaderParameterSetToInfo(jShaderInfo& InOutShaderInfo, uint32 InSpace)
{
    jShaderParameterSet::AppendToShaderInfo<TParameterSet>(InOutShaderInfo, InSpace);
}

#define JSHADER_CONCAT_INNER(A, B) A##B
#define JSHADER_CONCAT(A, B) JSHADER_CONCAT_INNER(A, B)

#define BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(TypeName) \
struct alignas(16) TypeName \
{ \
private: \
    using jShaderUniformBufferThisType = TypeName; \
    static constexpr int __jShaderUniformBufferBeginLine = __LINE__; \
public: \
    static constexpr const char* GetShaderUniformBufferTypeName() { return #TypeName; }

#define JSHADER_DECLARE_UNIFORM_BUFFER_MEMBER(Line, FieldType, Name) \
    alignas(TShaderUniformBufferFieldAlignmentInfo<FieldType>::Value()) FieldType Name{}; \
private: \
    struct JSHADER_CONCAT(__jShaderUniformBufferFieldName_, Line) \
    { \
        static constexpr const char* Get() { return #Name; } \
    }; \
public: \
    static constexpr jShaderUniformBufferFieldMeta __jShaderUniformBufferFieldMeta(TShaderUniformBufferFieldTag<jShaderUniformBufferThisType, Line>) \
    { \
        using jShaderUniformBufferFieldEntry = jShaderParameterDetail::TShaderUniformBufferFieldEntry<jShaderUniformBufferThisType, FieldType, &jShaderUniformBufferThisType::Name, JSHADER_CONCAT(__jShaderUniformBufferFieldName_, Line)>; \
        return jShaderUniformBufferFieldEntry::GetMeta(); \
    } \
public:

#define SHADER_UNIFORM_BUFFER_MEMBER(FieldType, Name) JSHADER_DECLARE_UNIFORM_BUFFER_MEMBER(__LINE__, FieldType, Name)

#define JSHADER_DECLARE_UNIFORM_BUFFER_MEMBER_ARRAY(Line, FieldType, Name, ArrayCount) \
    static_assert(TShaderUniformBufferArrayFieldInfo<FieldType>::IsSupported(), "Uniform buffer arrays currently require 16-byte stride-compatible field types."); \
    alignas(16) FieldType Name[ArrayCount] = {}; \
private: \
    struct JSHADER_CONCAT(__jShaderUniformBufferFieldArrayName_, Line) \
    { \
        static constexpr const char* Get() { return #Name; } \
    }; \
public: \
    static constexpr jShaderUniformBufferFieldMeta __jShaderUniformBufferFieldMeta(TShaderUniformBufferFieldTag<jShaderUniformBufferThisType, Line>) \
    { \
        return { JSHADER_CONCAT(__jShaderUniformBufferFieldArrayName_, Line)::Get(), TShaderUniformBufferFieldHLSLTypeInfo<FieldType>::GetTypeName(), ArrayCount, &TShaderParameterHLSLTypeInfo<FieldType>::AppendTypeDeclaration, true }; \
    } \
public:

#define SHADER_UNIFORM_BUFFER_MEMBER_ARRAY(FieldType, Name, ArrayCount) JSHADER_DECLARE_UNIFORM_BUFFER_MEMBER_ARRAY(__LINE__, FieldType, Name, ArrayCount)

#define END_SHADER_UNIFORM_BUFFER_STRUCT() \
private: \
    static constexpr int __jShaderUniformBufferEndLine = __LINE__; \
public: \
    static const auto& GetShaderUniformBufferFieldMembers() { static const auto ShaderUniformBufferFieldMembers = jShaderParameterDetail::CollectShaderUniformBufferFieldMembers<jShaderUniformBufferThisType, __jShaderUniformBufferBeginLine + 1, __jShaderUniformBufferEndLine>(); return ShaderUniformBufferFieldMembers; } \
};

#define BEGIN_SHADER_STRUCT(TypeName) \
struct alignas(16) TypeName \
{ \
private: \
    using jShaderUniformBufferThisType = TypeName; \
    static constexpr int __jShaderUniformBufferBeginLine = __LINE__; \
public: \
    static constexpr const char* GetShaderUniformBufferTypeName() { return #TypeName; }

#define JSHADER_DECLARE_STRUCT_MEMBER(Line, FieldType, Name) \
    FieldType Name{}; \
private: \
    struct JSHADER_CONCAT(__jShaderStructFieldName_, Line) \
    { \
        static constexpr const char* Get() { return #Name; } \
    }; \
public: \
    static constexpr jShaderUniformBufferFieldMeta __jShaderUniformBufferFieldMeta(TShaderUniformBufferFieldTag<jShaderUniformBufferThisType, Line>) \
    { \
        using jShaderUniformBufferFieldEntry = jShaderParameterDetail::TShaderUniformBufferFieldEntry<jShaderUniformBufferThisType, FieldType, &jShaderUniformBufferThisType::Name, JSHADER_CONCAT(__jShaderStructFieldName_, Line)>; \
        return jShaderUniformBufferFieldEntry::GetMeta(); \
    } \
public:

#define JSHADER_DECLARE_STRUCT_MEMBER_ARRAY(Line, FieldType, Name, ArrayCount) \
    FieldType Name[ArrayCount] = {}; \
private: \
    struct JSHADER_CONCAT(__jShaderStructFieldArrayName_, Line) \
    { \
        static constexpr const char* Get() { return #Name; } \
    }; \
public: \
    static constexpr jShaderUniformBufferFieldMeta __jShaderUniformBufferFieldMeta(TShaderUniformBufferFieldTag<jShaderUniformBufferThisType, Line>) \
    { \
        return { JSHADER_CONCAT(__jShaderStructFieldArrayName_, Line)::Get(), TShaderUniformBufferFieldHLSLTypeInfo<FieldType>::GetTypeName(), ArrayCount, &TShaderParameterHLSLTypeInfo<FieldType>::AppendTypeDeclaration, true }; \
    } \
public:

#define SHADER_STRUCT_MEMBER(FieldType, Name) JSHADER_DECLARE_STRUCT_MEMBER(__LINE__, FieldType, Name)
#define SHADER_STRUCT_MEMBER_ARRAY(FieldType, Name, ArrayCount) JSHADER_DECLARE_STRUCT_MEMBER_ARRAY(__LINE__, FieldType, Name, ArrayCount)
#define END_SHADER_STRUCT() END_SHADER_UNIFORM_BUFFER_STRUCT()

#define BEGIN_SHADER_PARAMETER_HLSL_TYPE(TypeName) \
template <> struct TShaderParameterHLSLTypeInfo<TypeName> \
{ \
    static constexpr const char* GetTypeName() { return #TypeName; } \
    static void AppendTypeDeclaration(std::string& Out) \
    {

#define HLSL_LINE(...) jShaderParameterDetail::AppendLine(Out, __VA_ARGS__)

#define END_SHADER_PARAMETER_HLSL_TYPE() \
    } \
};

#define BEGIN_SHADER_PARAMETER_SET(SetName) \
struct SetName \
{ \
private: \
    using jShaderParameterSetThisType = SetName; \
    static constexpr int __jShaderParameterBeginLine = __LINE__; \
public:

#define JSHADER_DECLARE_PARAMETER(Line, ParameterType, Name) \
    ParameterType Name{}; \
private: \
    struct JSHADER_CONCAT(__jShaderParameterName_, Line) \
    { \
        static constexpr const char* Get() { return #Name; } \
    }; \
public: \
    static constexpr jShaderParameterMemberMeta __jShaderParameterMeta(TShaderParameterTag<jShaderParameterSetThisType, Line>) \
    { \
        using jShaderParameterEntry = jShaderParameterDetail::TShaderParameterMemberEntry<jShaderParameterSetThisType, ParameterType, &jShaderParameterSetThisType::Name, JSHADER_CONCAT(__jShaderParameterName_, Line)>; \
        return jShaderParameterEntry::GetMeta(); \
    } \
public:

#define SHADER_PARAMETER(ParameterType, Name) JSHADER_DECLARE_PARAMETER(__LINE__, ParameterType, Name)
#define SHADER_TEXTURE2D(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterTexture2D, Name)
#define SHADER_TEXTURE2D_SRV(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterTexture2DSRV, Name)
#define SHADER_TEXTURECUBE(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterTextureCube, Name)
#define SHADER_TEXTURECUBE_SRV(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterTextureCubeSRV, Name)
#define SHADER_TEXTURE2D_COMPARISON(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterTexture2DComparison, Name)
#define SHADER_SUBPASS_INPUT(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterSubpassInput, Name)
#define SHADER_SUBPASS_INPUT_FLOAT(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterSubpassInputFloat, Name)
#define SHADER_TEXTURECUBE_COMPARISON(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterTextureCubeComparison, Name)
#define SHADER_SAMPLER(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterSampler, Name)
#define SHADER_ACCELERATION_STRUCTURE(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterAccelerationStructure, Name)
#define SHADER_RW_TEXTURE2D(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterRWTexture2D, Name)
#define SHADER_RW_TEXTURE2D_FLOAT2(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterRWTexture2DFloat2, Name)
#define SHADER_RW_TEXTURE2D_FLOAT(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterRWTexture2DFloat, Name)
#define SHADER_RW_TEXTURE2DARRAY(Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterRWTexture2DArray, Name)
#define SHADER_UNIFORM_BUFFER(BufferType, Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterUniformBuffer<BufferType>, Name)
#define SHADER_STRUCTURED_BUFFER(StructType, Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterStructuredBuffer<StructType>, Name)
#define SHADER_RW_STRUCTURED_BUFFER(StructType, Name) JSHADER_DECLARE_PARAMETER(__LINE__, jShaderParameterRWStructuredBuffer<StructType>, Name)

#define END_SHADER_PARAMETER_SET() \
private: \
    static constexpr int __jShaderParameterEndLine = __LINE__; \
public: \
    static const auto& GetShaderParameterMembers() { static const auto ShaderParameterMembers = jShaderParameterDetail::CollectShaderParameterMembers<jShaderParameterSetThisType, __jShaderParameterBeginLine + 1, __jShaderParameterEndLine>(); return ShaderParameterMembers; } \
};

#define BEGIN_SHADER_BINDLESS_SET(SetName) \
struct SetName \
{ \
private: \
    using jShaderBindlessSetThisType = SetName; \
    static constexpr int __jShaderBindlessBeginLine = __LINE__; \
public:

#define JSHADER_DECLARE_BINDLESS_PARAMETER(Line, ParameterType, Name, Space) \
    ParameterType Name{}; \
private: \
    struct JSHADER_CONCAT(__jShaderBindlessName_, Line) \
    { \
        static constexpr const char* Get() { return #Name; } \
    }; \
public: \
    static constexpr jShaderBindlessMemberMeta __jShaderBindlessMeta(TShaderBindlessTag<jShaderBindlessSetThisType, Line>) \
    { \
        using jShaderBindlessEntry = jShaderParameterDetail::TShaderBindlessMemberEntry<jShaderBindlessSetThisType, ParameterType, &jShaderBindlessSetThisType::Name, JSHADER_CONCAT(__jShaderBindlessName_, Line), Space>; \
        return jShaderBindlessEntry::GetMeta(); \
    } \
public:

#define SHADER_BINDLESS_BUFFER(HLSLType, Name, Space) JSHADER_DECLARE_BINDLESS_PARAMETER(__LINE__, jShaderParameterBindlessBuffer<HLSLType>, Name, Space)
#define SHADER_BINDLESS_STRUCTURED_BUFFER(StructType, Name, Space) JSHADER_DECLARE_BINDLESS_PARAMETER(__LINE__, jShaderParameterBindlessStructuredBuffer<StructType>, Name, Space)
#define SHADER_BINDLESS_BYTEADDRESS_BUFFER(Name, Space) JSHADER_DECLARE_BINDLESS_PARAMETER(__LINE__, jShaderParameterBindlessByteAddressBuffer, Name, Space)
#define SHADER_BINDLESS_UNIFORM_BUFFER(BufferType, Name, Space) JSHADER_DECLARE_BINDLESS_PARAMETER(__LINE__, jShaderParameterBindlessUniformBuffer<BufferType>, Name, Space)
#define SHADER_BINDLESS_TEXTURE2D(Name, Space) JSHADER_DECLARE_BINDLESS_PARAMETER(__LINE__, jShaderParameterBindlessTexture2DSRV, Name, Space)
#define SHADER_BINDLESS_TEXTURECUBE(Name, Space) JSHADER_DECLARE_BINDLESS_PARAMETER(__LINE__, jShaderParameterBindlessTextureCubeSRV, Name, Space)
#define SHADER_BINDLESS_SAMPLER(Name, Space) JSHADER_DECLARE_BINDLESS_PARAMETER(__LINE__, jShaderParameterBindlessSampler, Name, Space)

#define END_SHADER_BINDLESS_SET() \
private: \
    static constexpr int __jShaderBindlessEndLine = __LINE__; \
public: \
    static const auto& GetShaderBindlessMembers() { static const auto ShaderBindlessMembers = jShaderParameterDetail::CollectShaderBindlessMembers<jShaderBindlessSetThisType, __jShaderBindlessBeginLine + 1, __jShaderBindlessEndLine>(); return ShaderBindlessMembers; } \
};
