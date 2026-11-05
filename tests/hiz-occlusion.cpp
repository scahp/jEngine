#include "Renderer/Features/jHiZOcclusionMath.h"
#include <algorithm>
#include <iostream>
#include <limits>

namespace
{
using namespace jHiZOcclusion;
int Passed=0,Failed=0;
void Check(bool value,const char* name){if(value)++Passed;else{++Failed;std::cerr<<"FAIL: "<<name<<'\n';}}
BoxCorners Box(double x0,double y0,double z0,double x1,double y1,double z1)
{
    BoxCorners corners;for(std::size_t i=0;i<8;++i)corners[i]={(i&1)?x1:x0,(i&2)?y1:y0,(i&4)?z1:z0};return corners;
}
constexpr Matrix4 Identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void TestSettingsAndRanges()
{
    Settings settings,output;std::string error;Check(!settings.Enabled&&ValidateSettings(settings,output,error)&&output==settings,"Hi-Z defaults to disabled and validates without changing the snapshot");
    output.DepthBias=.004f;const auto original=output;settings.DepthBias=std::numeric_limits<float>::quiet_NaN();
    Check(!ValidateSettings(settings,output,error)&&output==original,"nonfinite depth bias preserves the last settings");
    settings={};settings.MaxCandidates=0;bool rejected=!ValidateSettings(settings,output,error)&&output==original;
    settings.MaxCandidates=MaximumCandidates+1;rejected&=!ValidateSettings(settings,output,error)&&output==original;
    settings={};settings.DepthBias=-.001f;rejected&=!ValidateSettings(settings,output,error)&&output==original;
    Check(rejected,"zero/over-budget candidates and negative bias are rejected transactionally");
    IndexedArguments args;
    Check(BuildIndexedArguments(900,600,{123,219,80,144},args,error)&&args==IndexedArguments{96,1,123,80,0},
        "real submesh first-index, count and base vertex are preserved in the 20-byte indirect command");
    Check(BuildIndexedArguments(36,24,{0,36,0,24},args,error)&&args==IndexedArguments{36,1,0,0,0},"whole primitive indirect command keeps its original index count");
    const auto previous=args;bool invalid=true;
    for(const IndexedRange range: {IndexedRange{-1,36,0,24},IndexedRange{0,37,0,24},IndexedRange{0,35,0,24},IndexedRange{0,36,-1,24},IndexedRange{0,36,0,25}})
        invalid&=!BuildIndexedArguments(36,24,range,args,error)&&args==previous;
    Check(invalid,"invalid submesh intervals cannot overwrite the last usable indirect arguments");
    std::array<float,8> colors{.1f,.2f,.3f,1.f,.4f,.5f,.6f,1.f};
    const auto bytes=[&](){return std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(colors.data()),sizeof(colors));};
    Check(VertexAlphaIsOpaque(bytes(),2,16,12),"known white legacy surfaces retain opaque vertex-color coverage even when RT alpha testing is enabled");
    colors[7]=.5f;bool transparent=!VertexAlphaIsOpaque(bytes(),2,16,12);colors[7]=std::numeric_limits<float>::quiet_NaN();
    Check(transparent&&!VertexAlphaIsOpaque(bytes(),2,16,12)&&!VertexAlphaIsOpaque(bytes(),3,16,12)&&!VertexAlphaIsOpaque(bytes(),2,16,15),
        "fractional/nonfinite vertex alpha and invalid coverage layouts cannot become opaque occluders");
}
void TestPyramid()
{
    std::string error;PyramidLayout layout;
    Check(BuildLayout(3,5,layout,error)&&layout.Levels[0]==PyramidLevel{4,8,0}&&layout.LevelCount==4,
        "odd NPOT depth is padded upward instead of dropping the last row or column");
    std::vector<float> depth(15,.2f),pyramid;
    Check(BuildPyramid(depth,layout,pyramid,error)&&pyramid.size()==layout.ElementCount&&pyramid[2+4*4]==.2f&&
        pyramid[3+4*4]==1.f&&pyramid.back()==1.f,"far-depth padding survives every maximum-reduction level");
    depth[14]=.9f;BuildPyramid(depth,layout,pyramid,error);
    Check(pyramid[layout.Levels[1].Offset+1+2*2]==1.f&&pyramid[2+4*4]==.9f,"NPOT edge geometry and padding are both retained conservatively");
    const auto old=pyramid;depth.pop_back();Check(!BuildPyramid(depth,layout,pyramid,error)&&pyramid==old,"incorrect depth length preserves the existing CPU reference pyramid");
    auto invalid=layout;invalid.Levels[1].Offset++;depth.resize(15,.2f);
    Check(!BuildPyramid(depth,invalid,pyramid,error)&&pyramid==old,"corrupt level layout cannot write or publish a partial pyramid");
    const auto previous=layout;Check(!BuildLayout(0,5,layout,error)&&layout==previous&&!BuildLayout(8193,5,layout,error)&&layout==previous,
        "invalid viewport dimensions retain the previous layout");
    Check(!BuildLayout(8192,8192,layout,error)&&layout==previous,"oversized padded viewport fails before allocating its depth pyramid");
    Check(BuildLayout(3840,2160,layout,error)&&layout.ResourceBytes<MaximumResourceBytes,"4K viewport depth and padded float pyramid remain within 128 MiB");
    BuildLayout(1,1,layout,error);depth={.25f};
    Check(BuildPyramid(depth,layout,pyramid,error)&&pyramid==depth&&IsOccluded({0,0,0,0,.75,true},layout,pyramid,1.e-5f),
        "one-pixel pyramid has a valid finite visibility query");
    BuildLayout(4,4,layout,error);depth.assign(16,.2f);depth[5]=std::numeric_limits<float>::quiet_NaN();
    Check(BuildPyramid(depth,layout,pyramid,error)&&pyramid[5]==1.f&&!IsOccluded({0,0,3,3,.9,true},layout,pyramid,0.f),
        "invalid source depth becomes far depth and cannot produce a false occlusion");
}
void TestProjectionAndVisibility()
{
    std::string error;ScreenBounds screen;
    Check(ProjectBounds(Box(-.25,-.25,.6,.25,.25,.8),Identity,64,32,true,screen,error)&&screen.Testable&&
        screen.MinimumX<=23&&screen.MaximumX>=41&&screen.MinimumY<=11&&screen.MaximumY>=21&&screen.NearestDepth<.6,
        "projected bounds expand in pixels and depth to cover float transform error");
    const auto baseline=screen;PyramidLayout layout;BuildLayout(64,32,layout,error);
    std::vector<float> depth(64*32,.2f),pyramid;BuildPyramid(depth,layout,pyramid,error);
    Check(IsOccluded(screen,layout,pyramid,1.e-5f),"full current-frame opaque coverage suppresses a farther draw");
    Check(!IsOccluded({screen.MinimumX,screen.MinimumY,screen.MaximumX,screen.MaximumY,.2,true},layout,pyramid,1.e-5f),
        "equal-depth/self coverage is retained by the strict conservative comparison");
    depth[16*64+32]=1.f;BuildPyramid(depth,layout,pyramid,error);
    Check(!IsOccluded(screen,layout,pyramid,1.e-5f),"clear-depth hole inside the screen rectangle prevents culling");
    depth.assign(64*32,.2f);depth[std::size_t(screen.MinimumY)*64+screen.MinimumX]=1.f;BuildPyramid(depth,layout,pyramid,error);
    Check(!IsOccluded(screen,layout,pyramid,1.e-5f),"clear-depth hole at a cell boundary cannot be missed by the four hierarchy taps");
    depth.assign(64*32,1.f);BuildPyramid(depth,layout,pyramid,error);
    Check(!IsOccluded(screen,layout,pyramid,1.e-5f),"current-frame occluder removal immediately restores the draw without history");
    bool uncertain=true;
    for(const auto& box:{Box(-.1,-.1,-.1,.1,.1,.3),Box(-1.2,-.1,.2,-.8,.1,.3),Box(-.1,-.1,.8,.1,.1,1.1)})
        uncertain&=ProjectBounds(box,Identity,64,32,true,screen,error)&&!screen.Testable&&!IsOccluded(screen,layout,pyramid,0.f);
    Check(uncertain,"near-plane, far-plane and partially off-screen bounds retain their ordinary draws");
    auto behind=Identity;behind[15]=-1;
    Check(ProjectBounds(Box(-.1,-.1,.2,.1,.1,.3),behind,64,32,true,screen,error)&&!screen.Testable,"nonpositive homogeneous w is never divided and culled");
    auto invalid=Identity;invalid[3]=std::numeric_limits<double>::infinity();screen=baseline;
    Check(!ProjectBounds(Box(-.1,-.1,.2,.1,.1,.3),invalid,64,32,true,screen,error)&&screen==baseline,"invalid projection matrix preserves the full screen-bounds result");
    auto badBox=Box(-.1,-.1,.2,.1,.1,.3);badBox[1][0]=std::numeric_limits<double>::quiet_NaN();
    Check(!ProjectBounds(badBox,Identity,64,32,true,screen,error)&&screen==baseline,"nonfinite world corner cannot publish partial bounds");
    ScreenBounds yUp,yDown;const auto upper=Box(-.1,.3,.2,.1,.5,.3);
    Check(ProjectBounds(upper,Identity,64,32,true,yUp,error)&&ProjectBounds(upper,Identity,64,32,false,yDown,error)&&yUp.Testable&&yDown.Testable&&
        yUp.MaximumY<16&&yDown.MinimumY>16,"texture Y orientation follows the real backend viewport convention");
    auto cancellation=Identity;cancellation[12]=1.e7;const auto large=Box(-1.e7-.1,-.1,.2,-1.e7+.1,.1,.3);
    Check(ProjectBounds(large,cancellation,64,32,true,screen,error)&&!screen.Testable,
        "large cancelling transforms fail open rather than assuming a tiny exact projected box");
    screen=baseline;auto corrupt=layout;corrupt.ElementCount--;
    Check(!IsOccluded(screen,corrupt,pyramid,0.f)&&!IsOccluded(screen,layout,pyramid,-.001f),"invalid query layout and bias fail open");
}
void TestDenseReference()
{
    // Differential test against all underlying pixels, not a second hierarchy
    // implementation: a culled box must have no uncovered pixel anywhere.
    std::uint32_t random=0x9834ae12;
    const auto next=[&](){random=random*1664525u+1013904223u;return random;};
    bool conservative=true;std::size_t suppressed=0,visible=0;std::string error;
    for(const auto size:{std::array<std::uint32_t,2>{31,23},{64,32},{3,5},{1,17}})
    {
        PyramidLayout layout;BuildLayout(size[0],size[1],layout,error);
        std::vector<float> depth(std::size_t(size[0])*size[1]),pyramid;
        for(std::uint32_t frame=0;frame<12;++frame)
        {
            for(auto& value:depth)value=frame%3==0?.2f:float(next()%1000)/1000.f;
            BuildPyramid(depth,layout,pyramid,error);
            for(std::uint32_t query=0;query<128;++query)
            {
                auto x0=next()%size[0],x1=next()%size[0],y0=next()%size[1],y1=next()%size[1];
                if(x0>x1)std::swap(x0,x1);if(y0>y1)std::swap(y0,y1);
                const double nearest=double(next()%1000)/1000;ScreenBounds bounds{x0,y0,x1,y1,nearest,true};
                float actualMaximum=0.f;for(auto y=y0;y<=y1;++y)for(auto x=x0;x<=x1;++x)actualMaximum=std::max(actualMaximum,depth[std::size_t(y)*size[0]+x]);
                const bool culled=IsOccluded(bounds,layout,pyramid,1.e-5f);
                conservative&=!culled||nearest>double(actualMaximum)+double(1.e-5f);
                suppressed+=culled;visible+=!culled;
            }
        }
    }
    Check(conservative&&suppressed>100&&visible>100,"6144 varying-frame NPOT queries agree conservatively with every covered base pixel");
}
}
int RunHiZOcclusionTests()
{
    Passed=0;Failed=0;
    TestSettingsAndRanges();TestPyramid();TestProjectionAndVisibility();TestDenseReference();
    std::cout<<"Hi-Z occlusion CPU: "<<Passed<<'/'<<Passed+Failed<<" passed\n";return Failed?1:0;
}
#ifndef JENGINE_HIZ_TEST_NO_MAIN
int main(){return RunHiZOcclusionTests();}
#endif
