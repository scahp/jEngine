#include "pch.h"
#include "jMaterialMapping.h"
#include "Code/Engine/Editor/jEditorTools.h"
#include "FileLoader/jEditorAssetPath.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace jMaterialMapping
{
namespace
{
using namespace jEditorTools;
enum class Type { Scalar, Float2, Float3, Float4, Texture, Visibility, Profile };
struct Checked { Type Kind = Type::Scalar; bool TextureValue = false; };
bool Numeric(Type type) { return type <= Type::Float4; }
bool Bounded(float value) { return std::isfinite(value) && std::abs(value) <= 1e8f; }
bool Integer(float value, float minimum, float maximum)
{ return std::isfinite(value) && value >= minimum && value <= maximum && std::trunc(value) == value; }
bool SafePath(const std::string& value)
{
    if (value.empty() || value.size() > 4096 || value.find_first_of("\\:") != std::string::npos) return false;
    for (unsigned char c : value) if (c < 0x20 || c == 0x7f) return false;
    for (size_t i = 0; i < value.size();)
    {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80) continue;
        int remaining = 0; uint32_t code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { remaining = 1; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { remaining = 2; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; code = first & 0x07; minimum = 0x10000; }
        else return false;
        if (i + remaining > value.size()) return false;
        while (remaining--)
        {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    try
    {
        const auto path = jEditorAssetPath::FromUtf8(value);
        if (path.is_absolute()) return false;
        for (const auto& part : path) if (part == "." || part == "..") return false;
        return true;
    }
    catch (...) { return false; }
}
const char* HlslType(Type type)
{
    switch (type)
    {
    case Type::Scalar: return "float";
    case Type::Float2: return "float2";
    case Type::Float3: return "float3";
    case Type::Float4: return "float4";
    case Type::Profile: return "uint";
    default: return "";
    }
}

class Compiler
{
public:
    explicit Compiler(const MaterialGraph& source) : Graph(source) {}
    Program Run()
    {
        if (Graph.Nodes.empty() || Graph.Nodes.size() > MaximumNodes) Fail("A material needs 1..256 nodes");
        for (const auto& node : Graph.Nodes)
        {
            if (!node.Id || node.Id == UINT32_MAX || !Nodes.emplace(node.Id, &node).second)
                Fail("Material node IDs must be unique nonzero values");
            if (int(node.Kind) < 0 || int(node.Kind) > int(NodeKind::DiffusionProfile)) Fail("Unknown material node kind");
            if (!Bounded(node.Scalar) || !Bounded(node.Color.X) || !Bounded(node.Color.Y) || !Bounded(node.Color.Z)
                || !Bounded(node.X) || !Bounded(node.Y)) Fail("Invalid material node number");
            for (float value : node.Parameters) if (!Bounded(value)) Fail("Invalid material node parameter");
            if (!node.Texture.empty() && !SafePath(node.Texture)) Fail("Material textures must be normalized project-relative paths");
            if (uint32_t(node.Semantic) > uint32_t(TextureSemantic::Normal)) Fail("Invalid texture semantic");
            if (uint32_t(node.NormalSpace) > uint32_t(NormalSpace::World)) Fail("Invalid normal coordinate space");
            for (float sign : node.InputBasisSignXYZ)
                if (sign != 1.f && sign != -1.f) Fail("Normal input basis signs must each be +1 or -1");
            if (node.Kind != NodeKind::TextureSampleNormal && (node.NormalSpace != NormalSpace::Tangent
                || node.InputBasisSignXYZ != std::array<float, 3>{1, 1, 1} || node.FlipNormalY))
                Fail("Normal metadata requires a Sample Normal node");
            Result.Active |= node.Kind > NodeKind::Add;
        }
        Result.Active |= Graph.Horizon != 0;
        Result.Active |= Graph.SubsurfaceWeight || Graph.SubsurfaceColor || Graph.SubsurfaceThickness || Graph.SubsurfaceProfile;
        Result.Active |= Graph.SpecularWeight || Graph.DiffuseOcclusion || Graph.PreScatterWeight;
        for (const auto& [id, node] : Nodes) Check(id);
        const auto base = Check(Graph.BaseColor).Kind;
        if (!Numeric(base) || base == Type::Float2) Fail("Base Color requires scalar, RGB or RGBA");
        if (Graph.Normal)
        {
            const auto normal = Check(Graph.Normal).Kind;
            if ((!Result.Active && Nodes.at(Graph.Normal)->Kind != NodeKind::Texture)
                || (Result.Active && normal != Type::Float3 && Nodes.at(Graph.Normal)->Kind != NodeKind::Texture))
                Fail("Normal requires a signed tangent-space RGB value or a legacy Texture node");
        }
        for (uint32_t output : {Graph.Metallic, Graph.Roughness}) if (output)
        {
            const auto checked = Check(output);
            if (!Numeric(checked.Kind) || checked.TextureValue || (Result.Active && checked.Kind != Type::Scalar))
                Fail("Metallic and Roughness require scalar values; use Channel for a texture sample");
        }
        if (Graph.Horizon && Check(Graph.Horizon).Kind != Type::Visibility) Fail("Horizon output requires a Horizon visibility node");
        for (const auto output : {Graph.SubsurfaceWeight, Graph.SubsurfaceThickness})
            if (output && Check(output).Kind != Type::Scalar) Fail("Subsurface weight and thickness require scalar inputs");
        if (Graph.SubsurfaceColor)
        {
            const auto type = Check(Graph.SubsurfaceColor).Kind;
            if (!Numeric(type) || type == Type::Float2) Fail("Subsurface color requires scalar, RGB or RGBA");
        }
        if (Graph.SubsurfaceProfile && Check(Graph.SubsurfaceProfile).Kind != Type::Profile)
            Fail("Subsurface profile requires a Diffusion Profile node");
        for (const auto output : {Graph.SpecularWeight, Graph.DiffuseOcclusion, Graph.PreScatterWeight})
            if (output && Check(output).Kind != Type::Scalar) Fail("Skin specular, occlusion and pre-scatter outputs require scalar inputs");
        if (Graph.Surface)
        {
            const auto& s = Graph.Surface->Shading;
            if (!Bounded(s.Subsurface) || s.Subsurface < 0 || s.Subsurface > 1
                || !Bounded(s.SubsurfaceThicknessWorld) || s.SubsurfaceThicknessWorld < 0 || s.SubsurfaceThicknessWorld > 10000
                || s.SubsurfaceProfile > 7) Fail("Invalid canonical subsurface surface settings");
            for (float color : s.SubsurfaceColor)
                if (!Bounded(color) || color < 0 || color > 1) Fail("Subsurface color must be finite linear RGB from 0 to 1");
            if (uint32_t(s.ShadingModel) > 1 || uint32_t(s.SubsurfaceSpace) > 1
                || !Bounded(s.SpecularWeight) || s.SpecularWeight < 0 || s.SpecularWeight > 8
                || !Bounded(s.DiffuseOcclusion) || s.DiffuseOcclusion < 0 || s.DiffuseOcclusion > 1
                || !Bounded(s.PreScatterWeight) || s.PreScatterWeight < 0 || s.PreScatterWeight > 1)
                Fail("Invalid canonical skin surface settings");
        }
        if (!Result.Active) return Result;

        Result.Hlsl = "#define JE_HAS_MATERIAL_GRAPH 1\n";
        if (Graph.Horizon) Result.Hlsl += "#define JE_HAS_HORIZON 1\n";
        Result.Hlsl += "#include \"Features/materialmapping.hlsl\"\n#include \"Features/materialmapping-functions.hlsl\"\n";
        Body.clear(); Emitted.clear();
        const auto color = Expression(Graph.BaseColor);
        std::string baseColor = color;
        if (base == Type::Scalar) baseColor = "float4((" + color + ").xxx,1.0)";
        else if (base == Type::Float3) baseColor = "float4(" + color + ",1.0)";
        std::string normal = "float3(0.0,0.0,1.0)";
        if (Graph.Normal)
        {
            if (Nodes.at(Graph.Normal)->Kind == NodeKind::Texture)
            {
                const auto slot = Texture(Graph.Normal, TextureSemantic::Normal);
                normal = "JE_DecodeNormalTS(" + TextureName(slot) + ".Sample(" + SamplerName(slot) + ",ctx.UV).rgb,1.0)";
            }
            else normal = Expression(Graph.Normal);
        }
        const auto metallic = Graph.Metallic ? Expression(Graph.Metallic) : "RenderObjectParam.Metallic";
        const auto roughness = Graph.Roughness ? Expression(Graph.Roughness) : "RenderObjectParam.Roughness";
        const auto sssWeight = Graph.SubsurfaceWeight ? Expression(Graph.SubsurfaceWeight) : "MaterialGraphParam.MaterialSubsurfaceColorWeight.w";
        std::string sssColor = Graph.SubsurfaceColor ? Expression(Graph.SubsurfaceColor) : "MaterialGraphParam.MaterialSubsurfaceColorWeight.rgb";
        if (Graph.SubsurfaceColor)
        {
            const auto type = Types.at(Graph.SubsurfaceColor).Kind;
            if (type == Type::Scalar) sssColor = "(" + sssColor + ").xxx";
            else if (type == Type::Float4) sssColor += ".rgb";
        }
        const auto sssThickness = Graph.SubsurfaceThickness ? Expression(Graph.SubsurfaceThickness) : "MaterialGraphParam.MaterialSubsurfaceParameters.x";
        const auto sssProfile = Graph.SubsurfaceProfile ? Expression(Graph.SubsurfaceProfile) : "(uint)MaterialGraphParam.MaterialSubsurfaceParameters.y";
        const auto specularWeight = Graph.SpecularWeight ? Expression(Graph.SpecularWeight) : "MaterialGraphParam.MaterialSkinParameters.y";
        const auto diffuseOcclusion = Graph.DiffuseOcclusion ? Expression(Graph.DiffuseOcclusion) : "MaterialGraphParam.MaterialSkinParameters.z";
        const auto preScatterWeight = Graph.PreScatterWeight ? Expression(Graph.PreScatterWeight) : "MaterialGraphParam.MaterialSkinParameters.w";
        const float opacity = Graph.Surface ? Graph.Surface->Opacity : 1.f;
        if (!std::isfinite(opacity) || opacity < 0 || opacity > 1) Fail("Invalid material opacity");
        Result.Hlsl += "JEMaterialSurface JE_EvaluateMaterial(JEMaterialContext ctx)\n{\n" + Body;
        Result.Hlsl += "  JEMaterialSurface surface=(JEMaterialSurface)0;\n  surface.BaseColor=saturate(" + baseColor + ");\n";
        Result.Hlsl += "  surface.NormalTS=JE_SafeNormalTS(" + normal + ");\n  surface.Metallic=saturate(" + metallic + ");\n";
        Result.Hlsl += "  surface.Roughness=saturate(" + roughness + ");\n";
        Result.Hlsl += "  surface.SubsurfaceWeight=saturate(" + sssWeight + ");\n  surface.SubsurfaceColor=saturate(" + sssColor + ");\n";
        Result.Hlsl += "  surface.SubsurfaceThickness=clamp(" + sssThickness + ",0.0,10000.0);\n  surface.SubsurfaceProfile=min((uint)(" + sssProfile + "),7u);\n";
        Result.Hlsl += "  surface.SpecularWeight=clamp(" + specularWeight + ",0.0,8.0);\n  surface.DiffuseOcclusion=saturate(" + diffuseOcclusion + ");\n";
        Result.Hlsl += "  surface.PreScatterWeight=saturate(" + preScatterWeight + ");\n  surface.ShadingFlags=(uint)MaterialGraphParam.MaterialSkinParameters.x;\n  return surface;\n}\n";
        Body.clear(); Emitted.clear();
        const auto horizon = Graph.Horizon ? Expression(Graph.Horizon) : "float2(1.0,1.0)";
        Result.Hlsl += "float2 JE_EvaluateHorizon(JEMaterialContext ctx,float3 LightDirWS)\n{\n" + Body + "  return " + horizon + ";\n}\n";
        Result.UsesHorizon = Graph.Horizon != 0;
        if (Result.Hlsl.size() > MaximumHlslBytes) Fail("Generated material exceeds 64 KiB");
        Result.CodeHash = 1469598103934665603ull;
        for (unsigned char c : Result.Hlsl) { Result.CodeHash ^= c; Result.CodeHash *= 1099511628211ull; }
        return std::move(Result);
    }

private:
    [[noreturn]] void Fail(const std::string& message) const { throw std::runtime_error(message); }
    Checked Check(uint32_t id)
    {
        const auto nodeIt = Nodes.find(id);
        if (nodeIt == Nodes.end()) Fail("Material input/output references a missing node");
        if (const auto checked = Types.find(id); checked != Types.end()) return checked->second;
        if (!Checking.insert(id).second) Fail("Material graph cycle rejected");
        const auto& node = *nodeIt->second;
        Checked result;
        const auto input = [&](uint32_t source, Type expected, const char* description)
        {
            if (!source || Check(source).Kind != expected) Fail(std::string(description) + " has the wrong or missing input type");
        };
        const auto& p = node.Parameters;
        switch (node.Kind)
        {
        case NodeKind::Color: result.Kind = Type::Float3; break;
        case NodeKind::Scalar: result.Kind = Type::Scalar; break;
        case NodeKind::Texture:
            if (!SafePath(node.Texture)) Fail("Texture requires a project-relative asset");
            result = {Type::Float4, true}; break;
        case NodeKind::UV: result.Kind = Type::Float2; break;
        case NodeKind::TextureObject:
            if (!SafePath(node.Texture)) Fail("Texture Object requires a project-relative asset");
            result.Kind = Type::Texture; break;
        case NodeKind::TextureSample:
        case NodeKind::TextureSampleNormal:
            input(node.InputA, Type::Texture, "Texture sample"); input(node.InputB, Type::Float2, "Texture sample UV");
            if (node.Kind == NodeKind::TextureSampleNormal && !Integer(p[0], 0, 1)) Fail("Normal Y flip must be 0 or 1");
            result = {node.Kind == NodeKind::TextureSampleNormal ? Type::Float3 : Type::Float4, true}; break;
        case NodeKind::Channel:
        {
            const auto source = Check(node.InputA).Kind;
            const float maxChannel = source == Type::Float4 ? 3.f : source == Type::Float3 ? 2.f : source == Type::Float2 ? 1.f : 0.f;
            if (!Numeric(source) || !Integer(p[0], 0, maxChannel)) Fail("Channel requires a numeric input and an in-range component");
            result.Kind = Type::Scalar; break;
        }
        case NodeKind::Multiply:
        case NodeKind::Add:
        {
            const auto a = Check(node.InputA), b = Check(node.InputB);
            if (!Numeric(a.Kind) || !Numeric(b.Kind)) Fail("Arithmetic accepts numeric values, never textures or Horizon visibility");
            if (!Result.Active && ((a.TextureValue && b.TextureValue) || (node.Kind == NodeKind::Add && (a.TextureValue || b.TextureValue))))
                Fail("Legacy graphs support one texture multiplied by a constant; use Texture Sample for a dynamic graph");
            if (a.Kind == b.Kind) result.Kind = a.Kind;
            else if (a.Kind == Type::Scalar) result.Kind = b.Kind;
            else if (b.Kind == Type::Scalar) result.Kind = a.Kind;
            else if ((a.Kind == Type::Float3 && b.Kind == Type::Float4) || (a.Kind == Type::Float4 && b.Kind == Type::Float3)) result.Kind = Type::Float4;
            else Fail("Cannot mix float2 with RGB/RGBA; arithmetic requires matching vector widths");
            result.TextureValue = a.TextureValue || b.TextureValue; break;
        }
        case NodeKind::Parallax:
        case NodeKind::POM:
        case NodeKind::Relief:
            input(node.InputA, Type::Float2, "Mapping UV"); input(node.InputB, Type::Texture, "Height/depth map");
            if (p[0] < 0 || p[0] > 1 || !Integer(p[1], 1, 128) || !Integer(p[2], 0, 8) || !Integer(p[3], 0, 1))
                Fail("Mapping needs scale 0..1, 1..128 steps, 0..8 refinements and mode 0 or 1");
            if (node.Kind == NodeKind::Parallax && p[2] != 0) Fail("Parallax refinement is reserved; use POM or Relief for binary refinement");
            result.Kind = Type::Float2; break;
        case NodeKind::Interior:
            input(node.InputA, Type::Float2, "Interior UV"); input(node.InputB, Type::Texture, "Interior color");
            if (node.InputC) input(node.InputC, Type::Texture, "Interior dual depth");
            if (node.InputD) input(node.InputD, Type::Texture, "Interior background");
            if (node.InputD && ((Nodes.at(node.InputB)->Semantic == TextureSemantic::Color) != (Nodes.at(node.InputD)->Semantic == TextureSemantic::Color)))
                Fail("Interior color and backdrop must use the same color encoding");
            if (p[0] <= 0 || p[0] > 256 || p[1] <= 0 || p[1] > 256 || p[2] < .001f || p[2] > 64 || p[3] < 0 || p[3] > 4)
                Fail("Interior needs positive room tiling up to 256, depth .001..64 and ambient 0..4");
            result = {Type::Float4, true}; break;
        case NodeKind::Horizon:
            input(node.InputA, Type::Float2, "Horizon UV"); input(node.InputB, Type::Texture, "Horizon layer 0");
            input(node.InputC, Type::Texture, "Horizon layer 1"); input(node.InputD, Type::Texture, "Horizon AO");
            if (p[0] < .001f || p[0] > 1.5707964f || p[1] < 0 || p[1] > 1 || p[2] < 0 || p[2] > 4 || p[3] != 0)
                Fail("Horizon needs softness .001..PI/2 radians, AO strength 0..1 and angle scale 0..4");
            result.Kind = Type::Visibility; break;
        case NodeKind::DiffusionProfile:
            if (!Integer(p[0], 0, 7)) Fail("Diffusion Profile must be an integer from 0 to 7");
            result.Kind = Type::Profile; break;
        }
        Checking.erase(id); Types[id] = result; return result;
    }
    uint32_t Value(uint32_t id, uint32_t role, const std::array<float, 4>& value)
    {
        const auto key = std::pair{id, role};
        if (const auto slot = ValueSlots.find(key); slot != ValueSlots.end()) return slot->second;
        if (Result.ValueCount >= MaximumValues) Fail("Material graph exceeds 64 float4 value slots");
        const uint32_t slot = Result.ValueCount++; Result.Values[slot] = value; ValueSlots[key] = slot; return slot;
    }
    uint32_t Texture(uint32_t id, TextureSemantic semantic)
    {
        const auto key = std::pair{id, semantic};
        if (const auto found = TextureSlots.find(key); found != TextureSlots.end()) return found->second;
        if (Result.Textures.size() >= MaximumTextures) Fail("Material graph exceeds 8 texture bindings");
        const auto& path = Nodes.at(id)->Texture;
        const auto slot = uint32_t(Result.Textures.size());
        Result.Textures.push_back({path, semantic}); TextureSlots[key] = slot;
        if (std::find(Result.Dependencies.begin(), Result.Dependencies.end(), path) == Result.Dependencies.end()) Result.Dependencies.push_back(path);
        return slot;
    }
    static std::string TextureName(uint32_t slot) { return "MappingTexture" + std::to_string(slot); }
    static std::string SamplerName(uint32_t slot) { return TextureName(slot) + "Sampler"; }
    std::string Parameters(const MaterialNode& node)
    { return "MaterialGraphParam.Values[" + std::to_string(Value(node.Id, 0, node.Parameters)) + "]"; }
    std::string Promote(const std::string& value, Type source, Type destination)
    {
        if (source == Type::Float3 && destination == Type::Float4) return "float4(" + value + ",1.0)";
        return value;
    }
    std::string Expression(uint32_t id)
    {
        if (const auto found = Emitted.find(id); found != Emitted.end()) return found->second;
        const auto& node = *Nodes.at(id); const auto type = Types.at(id).Kind;
        const auto sample = [&](uint32_t textureNode, const std::string& uv, TextureSemantic semantic)
        {
            const auto slot = Texture(textureNode, semantic);
            const auto value = TextureName(slot) + ".Sample(" + SamplerName(slot) + "," + uv + ")";
            return semantic == TextureSemantic::Color ? "JE_LinearColor(" + value + ")" : value;
        };
        std::string expression;
        switch (node.Kind)
        {
        case NodeKind::Color:
            expression = "MaterialGraphParam.Values[" + std::to_string(Value(id, 0, {node.Color.X, node.Color.Y, node.Color.Z, 1})) + "].rgb"; break;
        case NodeKind::Scalar:
            expression = "MaterialGraphParam.Values[" + std::to_string(Value(id, 0, {node.Scalar, 0, 0, 0})) + "].x"; break;
        case NodeKind::Texture: expression = sample(id, "ctx.UV", TextureSemantic::Color); break;
        case NodeKind::UV: expression = "ctx.UV*" + Parameters(node) + ".xy+" + Parameters(node) + ".zw"; break;
        case NodeKind::TextureObject: Fail("Texture objects may only connect to texture or mapping inputs");
        case NodeKind::TextureSample:
            expression = sample(node.InputA, Expression(node.InputB), Nodes.at(node.InputA)->Semantic); break;
        case NodeKind::TextureSampleNormal:
        {
            auto parameters = node.Parameters;
            parameters[0] = node.FlipNormalY ? (*node.FlipNormalY ? 1.f : 0.f)
                : node.NormalSpace == NormalSpace::Tangent ? node.Parameters[0] : 0.f;
            const auto value = "MaterialGraphParam.Values[" + std::to_string(Value(node.Id, 0, parameters)) + "]";
            const auto basis = "MaterialGraphParam.Values[" + std::to_string(Value(node.Id, 1,
                {node.InputBasisSignXYZ[0], node.InputBasisSignXYZ[1], node.InputBasisSignXYZ[2], float(node.NormalSpace)})) + "]";
            expression = "JE_DecodeMaterialNormal(" + sample(node.InputA, Expression(node.InputB), TextureSemantic::Normal)
                + ".rgb,ctx," + basis + "," + value + ".x)"; break;
        }
        case NodeKind::Channel:
        {
            const auto value = Expression(node.InputA);
            expression = Types.at(node.InputA).Kind == Type::Scalar ? value : value + "[(uint)" + Parameters(node) + ".x]";
            break;
        }
        case NodeKind::Multiply:
        case NodeKind::Add:
            expression = "clamp(" + Promote(Expression(node.InputA), Types.at(node.InputA).Kind, type) +
                (node.Kind == NodeKind::Multiply ? "*" : "+") + Promote(Expression(node.InputB), Types.at(node.InputB).Kind, type) + ",-1e8,1e8)"; break;
        case NodeKind::Parallax:
        case NodeKind::POM:
        case NodeKind::Relief:
        {
            const auto uv = Expression(node.InputA);
            const auto slot = Texture(node.InputB, TextureSemantic::Linear);
            const char* function = node.Kind == NodeKind::Parallax ? "JE_Parallax" : node.Kind == NodeKind::POM ? "JE_POM" : "JE_Relief";
            expression = std::string(function) + "(" + TextureName(slot) + "," + SamplerName(slot) + "," + uv + ",mul(transpose(ctx.TBN),ctx.ViewDirWS)," + Parameters(node) + ")";
            break;
        }
        case NodeKind::Interior:
        {
            const auto uv = Expression(node.InputA);
            const auto semantic = Nodes.at(node.InputB)->Semantic == TextureSemantic::Color ? TextureSemantic::Color : TextureSemantic::Linear;
            const auto color = Texture(node.InputB, semantic);
            const auto args = TextureName(color) + "," + SamplerName(color);
            if (node.InputC)
            {
                const auto depth = Texture(node.InputC, TextureSemantic::Linear);
                const auto background = node.InputD ? Texture(node.InputD, semantic) : color;
                expression = std::string(semantic == TextureSemantic::Color ? "JE_InteriorDual(" : "JE_InteriorDualLinear(")
                    + args + "," + TextureName(depth) + "," + SamplerName(depth) + "," + TextureName(background) + "," + SamplerName(background);
            }
            else expression = std::string(semantic == TextureSemantic::Color ? "JE_Interior(" : "JE_InteriorLinear(") + args;
            expression += "," + uv + ",mul(transpose(ctx.TBN),ctx.ViewDirWS)," + Parameters(node) + ")";
            Result.UsesInterior = true; break;
        }
        case NodeKind::Horizon:
        {
            const auto uv = Expression(node.InputA);
            const auto a = Texture(node.InputB, TextureSemantic::Linear), b = Texture(node.InputC, TextureSemantic::Linear), ao = Texture(node.InputD, TextureSemantic::Linear);
            expression = "JE_Horizon(" + TextureName(a) + "," + SamplerName(a) + "," + TextureName(b) + "," + SamplerName(b)
                + "," + TextureName(ao) + "," + SamplerName(ao) + "," + uv + ",mul(transpose(ctx.TBN),LightDirWS)," + Parameters(node) + ")";
            break;
        }
        case NodeKind::DiffusionProfile:
            expression = "(uint)" + Parameters(node) + ".x"; break;
        }
        const auto name = "je_node_" + std::to_string(id);
        const auto declaration = type == Type::Visibility ? "float2" : HlslType(type);
        Body += "  " + std::string(declaration) + " " + name + "=" + expression + ";\n";
        Emitted[id] = name; return name;
    }
    const MaterialGraph& Graph;
    Program Result;
    std::map<uint32_t, const MaterialNode*> Nodes;
    std::map<uint32_t, Checked> Types;
    std::set<uint32_t> Checking;
    std::map<uint32_t, std::string> Emitted;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> ValueSlots;
    std::map<std::pair<uint32_t, TextureSemantic>, uint32_t> TextureSlots;
    std::string Body;
};
}

bool Compile(const jEditorTools::MaterialGraph& graph, Program& output, std::string& error)
{
    try { auto result = Compiler(graph).Run(); output = std::move(result); error.clear(); return true; }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
} // namespace jMaterialMapping
