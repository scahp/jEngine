#include "Renderer/jEffectHistory.h"
#include <iostream>
#include <limits>
#include <cmath>

#ifndef JENGINE_EFFECT_AUDIT_STANDALONE
bool VerifySSGICopyBindingCallback();
#endif

namespace
{
struct TestEffectTarget
{
    struct { int Width = 65; int Height = 33; } Info;
};

namespace ShaderMath
{
    struct uint2 { unsigned int x, y; };
    struct float2
    {
        float x, y;
        float2(float X, float Y) : x(X), y(Y) {}
    };
    // Execute the production HLSL arithmetic with CPU vector value types.
    #include "../Resource/Shaders/hlsl/effect_sampling.hlsl"
}
}

int RunEffectsAuditTests()
{
    int Failures = 0;
    auto Expect = [&](bool Condition, const char* Name) {
        std::cout << (Condition ? "PASS " : "FAIL ") << Name << '\n';
        Failures += !Condition;
    };

    jEffectHistory<TestEffectTarget> History;
    auto A = std::make_shared<TestEffectTarget>();
    auto B = std::make_shared<TestEffectTarget>();
    auto C = std::make_shared<TestEffectTarget>();
    Expect(!History.GetPrevious(7, A), "first effect frame requires a GPU history seed");
    History.Commit(C, 7);
    Expect(History.GetPrevious(8, A) == C, "history follows actual output after nonsequential image acquisition");
    History.Commit(A, 8);
    Expect(History.GetPrevious(9, B) == A, "two image swapchain retains the preceding rendered output");
    Expect(!History.GetPrevious(10, B), "re-enabled effect rejects a skipped frame");
    Expect(!History.GetPrevious(9, A), "history cannot alias the writable output");
    B->Info.Width = 129;
    Expect(!History.GetPrevious(9, B), "resized effect rejects mismatched history");
    B->Info.Width = 65;
    History.Invalidate();
    Expect(!History.GetPrevious(9, B), "resource recreation invalidates history at equal dimensions");
    History.Commit(A, 8);
    A.reset();
    Expect(!History.GetPrevious(9, B), "history does not retain released render targets");
    History.Commit(C, std::numeric_limits<std::uint32_t>::max());
    Expect(History.GetPrevious(0, B) == C, "frame counter wrap preserves consecutive history");

    const auto SinglePixelUV = ShaderMath::EffectPixelUV({0, 0}, {1, 1});
    Expect(SinglePixelUV.x == 0.5f && SinglePixelUV.y == 0.5f,
        "one pixel effects sample the finite center of the source");
    const auto ThinUV = ShaderMath::EffectPixelUV({0, 1}, {1, 3});
    Expect(ThinUV.x == 0.5f && ThinUV.y == 0.5f,
        "one pixel axes remain centered independently");
    bool FiniteInteriorUVs = true;
    for (unsigned int Width : {1U, 2U, 3U, 7U, 8U, 9U, 65U})
    {
        for (unsigned int Height : {1U, 2U, 3U, 7U, 8U, 9U, 33U})
        {
            for (unsigned int Y = 0; Y < Height; ++Y)
            {
                for (unsigned int X = 0; X < Width; ++X)
                {
                    const auto UV = ShaderMath::EffectPixelUV({X, Y}, {Width, Height});
                    FiniteInteriorUVs &= std::isfinite(UV.x) && std::isfinite(UV.y)
                        && UV.x > 0.0f && UV.x < 1.0f && UV.y > 0.0f && UV.y < 1.0f;
                }
            }
        }
    }
    Expect(FiniteInteriorUVs, "all tiny and odd extent pixels map inside the source texture");
#ifndef JENGINE_EFFECT_AUDIT_STANDALONE
    Expect(VerifySSGICopyBindingCallback(), "Gaussian SSGI copy callbacks bind exactly one texture matching CopyPS");
#endif
    return Failures ? 1 : 0;
}

#ifdef JENGINE_EFFECT_AUDIT_STANDALONE
int main() { return RunEffectsAuditTests(); }
#endif
