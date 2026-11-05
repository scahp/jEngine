#include "jHiZOcclusionMath.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace jHiZOcclusion
{
namespace
{
bool Reject(std::string& error,const char* text) { error=text;return false; }
std::uint32_t PowerOfTwo(std::uint32_t value)
{std::uint32_t result=1;while(result<value)result*=2;return result;}
bool LayoutValid(const PyramidLayout& layout) noexcept
{
    if(!layout.ViewWidth||!layout.ViewHeight||layout.ViewWidth>MaximumDimension||layout.ViewHeight>MaximumDimension||
        !layout.LevelCount||layout.LevelCount>MaximumLevels)return false;
    auto width=PowerOfTwo(layout.ViewWidth),height=PowerOfTwo(layout.ViewHeight);std::size_t offset=0;
    for(std::uint32_t level=0;level<layout.LevelCount;++level)
    {
        if(layout.Levels[level]!=PyramidLevel{width,height,static_cast<std::uint32_t>(offset)})return false;
        offset+=std::size_t(width)*height;
        if(width==1&&height==1)
            return level+1==layout.LevelCount&&offset==layout.ElementCount&&
                layout.ResourceBytes==(offset+std::size_t(layout.ViewWidth)*layout.ViewHeight)*sizeof(float)&&
                layout.ResourceBytes<=MaximumResourceBytes;
        width=std::max(1u,width/2);height=std::max(1u,height/2);
    }
    return false;
}
}
bool BuildIndexedArguments(std::uint32_t indices,std::uint32_t vertices,const IndexedRange& range,IndexedArguments& output,std::string& error)
{
    error.clear();
    if(!indices||!vertices||range.FirstIndex<0||range.EndIndex<=range.FirstIndex||range.EndIndex>indices||
        range.BaseVertex<0||range.EndVertex<=range.BaseVertex||range.EndVertex>vertices||range.BaseVertex>std::numeric_limits<std::int32_t>::max()||
        (range.EndIndex-range.FirstIndex)%3!=0)
        return Reject(error,"Hi-Z indexed submesh range is invalid or outside its retained mesh.");
    output={std::uint32_t(range.EndIndex-range.FirstIndex),1,std::uint32_t(range.FirstIndex),std::int32_t(range.BaseVertex),0};return true;
}
bool VertexAlphaIsOpaque(std::span<const unsigned char> bytes,std::size_t count,std::size_t stride,std::size_t alphaOffset) noexcept
{
    if(!count||count>MaximumScannedVertices||stride<sizeof(float)||alphaOffset>stride-sizeof(float)||
        count>std::numeric_limits<std::size_t>::max()/stride||bytes.size()<(count-1)*stride+alphaOffset+sizeof(float))return false;
    for(std::size_t i=0;i<count;++i)
    {
        float alpha;std::memcpy(&alpha,bytes.data()+i*stride+alphaOffset,sizeof(alpha));
        if(!std::isfinite(alpha)||alpha<1.f)return false;
    }
    return true;
}
bool BuildLayout(std::uint32_t width,std::uint32_t height,PyramidLayout& output,std::string& error)
{
    error.clear();
    if(!width||!height||width>MaximumDimension||height>MaximumDimension)
        return Reject(error,"Hi-Z viewport must be between 1 and 8192 pixels per dimension.");
    PyramidLayout candidate;candidate.ViewWidth=width;candidate.ViewHeight=height;
    auto paddedWidth=PowerOfTwo(width),paddedHeight=PowerOfTwo(height);
    if(std::size_t(paddedWidth)*paddedHeight>16*1024*1024)
        return Reject(error,"Hi-Z padded depth exceeds its 16-million-pixel budget.");
    std::size_t offset=0;
    for(;;)
    {
        if(candidate.LevelCount==MaximumLevels)return Reject(error,"Hi-Z pyramid exceeds its level budget.");
        candidate.Levels[candidate.LevelCount++]={paddedWidth,paddedHeight,static_cast<std::uint32_t>(offset)};
        offset+=std::size_t(paddedWidth)*paddedHeight;
        if(paddedWidth==1&&paddedHeight==1)break;
        paddedWidth=std::max(1u,paddedWidth/2);paddedHeight=std::max(1u,paddedHeight/2);
    }
    candidate.ElementCount=static_cast<std::uint32_t>(offset);
    candidate.ResourceBytes=(offset+std::size_t(width)*height)*sizeof(float);
    if(candidate.ResourceBytes>MaximumResourceBytes)return Reject(error,"Hi-Z depth and pyramid exceed 128 MiB.");
    output=candidate;return true;
}
bool BuildPyramid(std::span<const float> depth,const PyramidLayout& layout,std::vector<float>& output,std::string& error)
{
    error.clear();
    if(!LayoutValid(layout)||depth.size()!=std::size_t(layout.ViewWidth)*layout.ViewHeight)
        return Reject(error,"Hi-Z input depth size or pyramid layout is invalid.");
    std::vector<float> candidate(layout.ElementCount,1.f);
    for(std::uint32_t y=0;y<layout.ViewHeight;++y)for(std::uint32_t x=0;x<layout.ViewWidth;++x)
    {
        const float value=depth[std::size_t(y)*layout.ViewWidth+x];
        candidate[std::size_t(y)*layout.Levels[0].Width+x]=std::isfinite(value)&&value>=0.f&&value<=1.f?value:1.f;
    }
    for(std::uint32_t level=1;level<layout.LevelCount;++level)
    {
        const auto& source=layout.Levels[level-1];const auto& destination=layout.Levels[level];
        for(std::uint32_t y=0;y<destination.Height;++y)for(std::uint32_t x=0;x<destination.Width;++x)
        {
            float maximum=0.f;
            for(std::uint32_t dy=0;dy<2;++dy)for(std::uint32_t dx=0;dx<2;++dx)
                maximum=std::max(maximum,candidate[source.Offset+std::size_t(std::min(2*y+dy,source.Height-1))*source.Width+
                    std::min(2*x+dx,source.Width-1)]);
            candidate[destination.Offset+std::size_t(y)*destination.Width+x]=maximum;
        }
    }
    output=std::move(candidate);return true;
}
bool ProjectBounds(const BoxCorners& corners,const Matrix4& matrix,std::uint32_t width,std::uint32_t height,bool ndcYUp,
    ScreenBounds& output,std::string& error)
{
    error.clear();
    if(!width||!height||width>MaximumDimension||height>MaximumDimension||
        !std::all_of(matrix.begin(),matrix.end(),[](double value){return std::isfinite(value)&&std::abs(value)<=1.e12;}))
        return Reject(error,"Hi-Z projection requires a finite bounded matrix and viewport.");
    for(const auto& point:corners)for(double value:point)
        if(!std::isfinite(value)||std::abs(value)>1.e9)return Reject(error,"Hi-Z world bounds are nonfinite or out of bounds.");
    ScreenBounds candidate;double minimumX=1.,minimumY=1.,maximumX=-1.,maximumY=-1.,nearest=1.;
    // Bound ordinary float matrix-vector rounding, even when cancellation makes
    // clip coordinates small. Caller also expands world boxes for M*vertex.
    constexpr double gamma=32.*std::numeric_limits<float>::epsilon();
    for(const auto& point:corners)
    {
        std::array<double,4> lower{},upper{};
        for(std::size_t row=0;row<4;++row)
        {
            double value=matrix[12+row],magnitude=std::abs(value);
            for(std::size_t axis=0;axis<3;++axis)
            {const double product=matrix[axis*4+row]*point[axis];value+=product;magnitude+=std::abs(product);}
            const double margin=gamma*std::max(magnitude,1.e-20);
            lower[row]=value-margin;upper[row]=value+margin;
        }
        if(lower[3]<=1.e-6||lower[2]<=0.||upper[2]>=lower[3]){output=candidate;return true;}
        const auto divided=[&](std::size_t axis)
        {
            const std::array<double,4> values{lower[axis]/lower[3],lower[axis]/upper[3],upper[axis]/lower[3],upper[axis]/upper[3]};
            return std::array<double,2>{*std::min_element(values.begin(),values.end()),*std::max_element(values.begin(),values.end())};
        };
        const auto x=divided(0),y=divided(1);
        minimumX=std::min(minimumX,x[0]);maximumX=std::max(maximumX,x[1]);
        minimumY=std::min(minimumY,y[0]);maximumY=std::max(maximumY,y[1]);
        nearest=std::min(nearest,lower[2]/upper[3]);
    }
    if(minimumX<=-1.||maximumX>=1.||minimumY<=-1.||maximumY>=1.){output=candidate;return true;}
    if(ndcYUp){const auto oldMinimum=minimumY;minimumY=-maximumY;maximumY=-oldMinimum;}
    const auto lowerPixel=[](double ndc,std::uint32_t size)
    {return static_cast<std::uint32_t>(std::max(0.,std::floor((ndc*.5+.5)*size)-1.));};
    const auto upperPixel=[](double ndc,std::uint32_t size)
    {return static_cast<std::uint32_t>(std::min(double(size-1),std::ceil((ndc*.5+.5)*size)+1.));};
    candidate.MinimumX=lowerPixel(minimumX,width);candidate.MaximumX=upperPixel(maximumX,width);
    candidate.MinimumY=lowerPixel(minimumY,height);candidate.MaximumY=upperPixel(maximumY,height);
    candidate.NearestDepth=std::max(0.,nearest);candidate.Testable=true;
    output=candidate;return true;
}
bool IsOccluded(const ScreenBounds& bounds,const PyramidLayout& layout,std::span<const float> pyramid,float bias) noexcept
{
    if(!bounds.Testable||!LayoutValid(layout)||pyramid.size()!=layout.ElementCount||
        bounds.MinimumX>bounds.MaximumX||bounds.MinimumY>bounds.MaximumY||bounds.MaximumX>=layout.ViewWidth||bounds.MaximumY>=layout.ViewHeight||
        !std::isfinite(bounds.NearestDepth)||bounds.NearestDepth<0.||bounds.NearestDepth>1.||!std::isfinite(bias)||bias<0.f||bias>.01f)return false;
    std::uint32_t level=0;
    while(level+1<layout.LevelCount&&((bounds.MaximumX>>level)-(bounds.MinimumX>>level)>1||
        (bounds.MaximumY>>level)-(bounds.MinimumY>>level)>1))++level;
    const auto& source=layout.Levels[level];
    const std::uint32_t x0=bounds.MinimumX>>level,x1=bounds.MaximumX>>level,y0=bounds.MinimumY>>level,y1=bounds.MaximumY>>level;
    float maximum=0.f;
    for(const auto y:{y0,y1})for(const auto x:{x0,x1})
    {
        if(x>=source.Width||y>=source.Height)return false;
        const float value=pyramid[source.Offset+std::size_t(y)*source.Width+x];
        if(!std::isfinite(value)||value<0.f||value>1.f)return false;
        maximum=std::max(maximum,value);
    }
    return bounds.NearestDepth>double(maximum)+double(bias);
}
}
