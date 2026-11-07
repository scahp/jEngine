#include "pch.h"
#include "jExperimentImage.h"
#include "lodepng.h"
#include <fstream>
#include <memory>
#include <cstdlib>
#include <stdexcept>

namespace jRenderingExperiment
{
void SavePng(const std::filesystem::path& path,const DisplayImage& image)
{
    if(!image.Width||!image.Height||uint64_t(image.Width)*image.Height>4194304||image.Rgba.size()!=uint64_t(image.Width)*image.Height*4)
        throw std::runtime_error("Invalid display image");
    unsigned char* bytes=nullptr;size_t size=0;
    const auto result=LodePNG_encode32(&bytes,&size,image.Rgba.data(),image.Width,image.Height);
    std::unique_ptr<unsigned char,decltype(&std::free)> data(bytes,std::free);
    if(result)throw std::runtime_error("Could not encode experiment PNG");
    std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::failbit|std::ios::badbit);
    file.write(reinterpret_cast<const char*>(data.get()),std::streamsize(size));
}
}
