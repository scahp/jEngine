#include "jExperimentImage.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace jRenderingExperiment
{
namespace
{
void Check(const FloatImage& image)
{
    const uint64_t pixels=uint64_t(image.Width)*image.Height;
    if(!pixels||pixels>4194304||image.Rgba.size()!=pixels*4)throw std::runtime_error("Invalid or oversized HDR image");
}
DisplayImage Destination(const FloatImage& image,uint32_t maxWidth)
{
    Check(image); DisplayImage out;
    out.Width=maxWidth?std::min(maxWidth,image.Width):image.Width;
    out.Height=std::max(1u,uint32_t(uint64_t(image.Height)*out.Width/image.Width));
    out.Rgba.resize(uint64_t(out.Width)*out.Height*4);return out;
}
uint8_t Byte(double value){return uint8_t(std::clamp(std::round(value*255),0.,255.));}
}
DisplayImage Display(const FloatImage& image,double exposure,uint32_t maxWidth)
{
    if(!std::isfinite(exposure)||std::abs(exposure)>16)throw std::runtime_error("Invalid display exposure");
    auto out=Destination(image,maxWidth);const double multiplier=std::exp2(exposure);
    for(uint32_t y=0;y<out.Height;++y)for(uint32_t x=0;x<out.Width;++x)
    {
        const auto src=(uint64_t(y)*image.Height/out.Height*image.Width+uint64_t(x)*image.Width/out.Width)*4;
        const auto dst=(uint64_t(y)*out.Width+x)*4;
        bool finite=true;for(size_t c=0;c<3;++c)finite&=std::isfinite(image.Rgba[src+c]);
        if(!finite){out.Rgba[dst]=255;out.Rgba[dst+1]=0;out.Rgba[dst+2]=255;}
        else for(size_t c=0;c<3;++c){const double value=std::max(0.,double(image.Rgba[src+c]))*multiplier;out.Rgba[dst+c]=Byte(std::pow(value/(1+value),1/2.2));}
        out.Rgba[dst+3]=255;
    }
    return out;
}
DisplayImage Heatmap(const FloatImage& a,const FloatImage& b,double gain,uint32_t maxWidth)
{
    Check(a);Check(b);if(a.Width!=b.Width||a.Height!=b.Height||!std::isfinite(gain)||gain<=0)throw std::runtime_error("Invalid heatmap inputs");
    auto out=Destination(a,maxWidth);
    for(uint32_t y=0;y<out.Height;++y)for(uint32_t x=0;x<out.Width;++x)
    {
        const auto src=(uint64_t(y)*a.Height/out.Height*a.Width+uint64_t(x)*a.Width/out.Width)*4;
        const auto dst=(uint64_t(y)*out.Width+x)*4;double difference=0;bool finite=true;
        for(size_t c=0;c<3;++c){finite&=std::isfinite(a.Rgba[src+c])&&std::isfinite(b.Rgba[src+c]);difference=std::max(difference,std::abs(double(a.Rgba[src+c])-b.Rgba[src+c]));}
        const double t=std::clamp(difference*gain,0.,1.);
        out.Rgba[dst]=finite?Byte(std::min(1.,t*2)):255;out.Rgba[dst+1]=finite?Byte(std::max(0.,t*2-1)):0;
        out.Rgba[dst+2]=finite?0:255;out.Rgba[dst+3]=255;
    }
    return out;
}
void SavePfm(const std::filesystem::path& path,const FloatImage& image)
{
    static_assert(std::endian::native==std::endian::little);
    Check(image);std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::failbit|std::ios::badbit);
    file<<"PF\n"<<image.Width<<' '<<image.Height<<"\n-1.0\n";
    std::vector<float> row(size_t(image.Width)*3);
    for(uint32_t y=image.Height;y-->0;)
    {
        for(uint32_t x=0;x<image.Width;++x)for(size_t c=0;c<3;++c)row[size_t(x)*3+c]=image.Rgba[(uint64_t(y)*image.Width+x)*4+c];
        file.write(reinterpret_cast<const char*>(row.data()),std::streamsize(row.size()*sizeof(float)));
    }
}
FloatImage LoadPfm(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary);file.exceptions(std::ios::failbit|std::ios::badbit);
    std::string magic;float scale;FloatImage image;file>>magic>>image.Width>>image.Height>>scale;
    if(magic!="PF"||scale!=-1.f||!image.Width||!image.Height||uint64_t(image.Width)*image.Height>4194304)
        throw std::runtime_error("Unsupported experiment PFM");
    if(file.get()!='\n')throw std::runtime_error("Invalid PFM header terminator");
    image.Rgba.resize(uint64_t(image.Width)*image.Height*4,1.f);std::vector<float> row(size_t(image.Width)*3);
    for(uint32_t y=image.Height;y-->0;)
    {
        file.read(reinterpret_cast<char*>(row.data()),std::streamsize(row.size()*sizeof(float)));
        for(uint32_t x=0;x<image.Width;++x)for(size_t c=0;c<3;++c)image.Rgba[(uint64_t(y)*image.Width+x)*4+c]=row[size_t(x)*3+c];
    }
    return image;
}
uint64_t NonFinitePixels(const FloatImage& image)
{
    Check(image);uint64_t count=0;
    for(size_t i=0;i<image.Rgba.size();i+=4)count+=!std::isfinite(image.Rgba[i])||!std::isfinite(image.Rgba[i+1])||!std::isfinite(image.Rgba[i+2]);
    return count;
}
}
