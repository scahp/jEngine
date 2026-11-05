#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jEditorTools { struct MaterialGraph; }

// Immutable CPU compilation data. GPU resources belong to each material instance.
namespace jMaterialMapping
{
inline constexpr size_t MaximumNodes = 256;
inline constexpr size_t MaximumTextures = 8;
inline constexpr size_t MaximumValues = 64;
inline constexpr size_t MaximumHlslBytes = 64 * 1024;

enum class TextureSemantic : uint32_t { Linear = 0, Color = 1, Normal = 2 };
enum class NormalSpace : uint32_t { Tangent = 0, Object = 1, World = 2 };
enum class ShadingModel : uint32_t { StandardPBR = 0, SkinKelemen = 1 };
enum class SubsurfaceSpace : uint32_t { Screen = 0, Texture = 1 };
enum OutputOverride : uint32_t
{
    OverrideBaseColor = 1u << 0,
    OverrideNormal = 1u << 1,
    OverrideMetallic = 1u << 2,
    OverrideRoughness = 1u << 3,
    OverrideBaseColorTexture = 1u << 4,
    OverrideSubsurfaceWeight = 1u << 5,
    OverrideSubsurfaceColor = 1u << 6,
    OverrideSubsurfaceThickness = 1u << 7,
    OverrideSubsurfaceProfile = 1u << 8,
    OverrideSpecularWeight = 1u << 9,
    OverrideDiffuseOcclusion = 1u << 10,
    OverridePreScatterWeight = 1u << 11,
    OverrideShadingModel = 1u << 12,
    OverrideSubsurfaceSpace = 1u << 13,
};

struct TextureBinding
{
    std::string Path;
    TextureSemantic Semantic = TextureSemantic::Linear;
    bool operator==(const TextureBinding&) const = default;
};

struct Program
{
    uint32_t Version = 1;
    bool Active = false, UsesHorizon = false, UsesInterior = false;
    // Depends only on generated code/topology, never texture paths or values.
    uint64_t CodeHash = 0;
    std::string Hlsl;
    std::vector<TextureBinding> Textures;
    std::vector<std::string> Dependencies;
    std::array<std::array<float, 4>, MaximumValues> Values{};
    uint32_t ValueCount = 0;
};

// Validate the whole DAG, including disconnected nodes. Failure preserves output.
bool Compile(const jEditorTools::MaterialGraph& graph, Program& output, std::string& error);
} // namespace jMaterialMapping
