#include "pch.h"
#include "RHI/jPipelineStateInfo.h"
#include "Renderer/jBloomDimensions.h"
#include "RHI/Vulkan/jDescriptorPoolSizing_Vulkan.h"

int RunGraphicsAuditTests()
{
    int Failures = 0;
    const auto Expect = [&](bool Condition, const char* Name) {
        std::cout << (Condition ? "PASS " : "FAIL ") << Name << std::endl;
        Failures += !Condition;
    };

    // These CPU state objects require no RHI initialization or GPU handles.
    jRasterizationStateInfo Rasterization;
    jDepthStencilStateInfo DepthStencil;
    jBlendingStateInfo Blending;
    std::vector<jViewport> Viewports{
        jViewport(3, 7, 65, 33, 0.2f, 0.9f),
        jViewport(-4, 2, 31, 17, 0.0f, 1.0f)
    };
    std::vector<jScissor> Scissors{jScissor(5, 9, 63, 31), jScissor(0, 1, 29, 15)};
    const jPipelineStateFixedInfo Fixed(&Rasterization, &DepthStencil, &Blending, Viewports, Scissors, true);

    Expect(Fixed.Viewports.size() == 2
        && Fixed.Viewports[0].X == 3 && Fixed.Viewports[0].Y == 7
        && Fixed.Viewports[0].Width == 65 && Fixed.Viewports[0].Height == 33
        && Fixed.Viewports[0].MinDepth == 0.2f && Fixed.Viewports[0].MaxDepth == 0.9f
        && Fixed.Viewports[1].X == -4 && Fixed.Viewports[1].Width == 31,
        "pipeline vector constructor retains both supplied viewports");
    Expect(Fixed.Scissors.size() == 2
        && Fixed.Scissors[0].Offset.x == 5 && Fixed.Scissors[0].Offset.y == 9
        && Fixed.Scissors[0].Extent.x == 63 && Fixed.Scissors[0].Extent.y == 31
        && Fixed.Scissors[1].Extent.x == 29 && Fixed.Scissors[1].Extent.y == 15,
        "pipeline vector constructor retains both supplied scissors");
    Viewports[0].X = 100;
    Scissors[0].Extent.x = 100;
    Expect(!Fixed.Viewports.empty() && Fixed.Viewports[0].X == 3 && Fixed.Scissors[0].Extent.x == 63,
        "pipeline owns independent copies of viewport and scissor vectors");

    struct BloomCase { int Dimension; std::array<int, 4> Expected; };
    const BloomCase Cases[] = {
        {1, {1, 1, 1, 1}},
        {3, {1, 1, 1, 1}},
        {7, {1, 1, 1, 1}},
        {31, {7, 3, 1, 1}},
        {32, {8, 4, 2, 1}},
        {33, {8, 4, 2, 1}},
        {65, {16, 8, 4, 2}},
        {1280, {320, 160, 80, 40}},
        {720, {180, 90, 45, 22}}
    };
    for (const auto& Case : Cases)
    {
        std::array<int, 4> Actual;
        Actual[0] = jBloomDimensions::Downsample(Case.Dimension, 4);
        for (size_t i = 1; i < Actual.size(); ++i)
            Actual[i] = jBloomDimensions::Downsample(Actual[i - 1], 2);
        const std::string Name = "bloom axis " + std::to_string(Case.Dimension) + " retains valid downsample dimensions";
        Expect(Actual == Case.Expected, Name.c_str());
    }
    jShaderBindingArray Bindings;
    Bindings.Add(jShaderBinding::CreateBindless(0, 2808, EShaderBindingType::TEXTURE_SRV, EShaderAccessStageFlag::COMPUTE));
    Bindings.Add(jShaderBinding::Create(1, 7, EShaderBindingType::BUFFER_UAV, EShaderAccessStageFlag::COMPUTE));
    Bindings.Add(jShaderBinding::CreateBindless(2, 104, EShaderBindingType::TEXTURE_ARRAY_SRV, EShaderAccessStageFlag::COMPUTE));
    Bindings.Add(jShaderBinding::Create(3, 37, EShaderBindingType::BUFFER_SRV, EShaderAccessStageFlag::COMPUTE));
    std::map<VkDescriptorType, uint32> Counts;
    for (const auto& Size : jVulkanDescriptorPoolSizing::GetRequirements(Bindings, 2))
        Counts[Size.type] = Size.descriptorCount;
    Expect(Counts.size() == 2 && Counts[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] == 5824
        && Counts[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER] == 88,
        "descriptor budget sums array elements and merges equivalent nonadjacent binding types");
    Expect(jVulkanDescriptorPoolSizing::GetRequirements({}, 1).empty(), "empty descriptor layout has an empty budget");
    Counts.clear();
    for (const auto& Budget : jVulkanDescriptorPoolSizing::DefaultPoolSizes)
        Counts[Budget.Type] = static_cast<uint32>(Budget.PerSet * 10000);
    Expect(Counts.size() == std::size(jVulkanDescriptorPoolSizing::DefaultPoolSizes)
        && Counts[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] == 20000
        && Counts[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE] == 1250
        && Counts[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] == 40000,
        "default descriptor ratios use explicit unique Vulkan types");
    return Failures ? 1 : 0;
}

#ifdef JENGINE_GRAPHICS_AUDIT_STANDALONE
int main() { return RunGraphicsAuditTests(); }
#endif
