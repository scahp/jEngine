#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace jRenderingExperiment
{
struct FloatImage { uint32_t Width = 0, Height = 0; std::vector<float> Rgba; };
struct DisplayImage { uint32_t Width = 0, Height = 0; std::vector<uint8_t> Rgba; };
DisplayImage Display(const FloatImage& image, double exposure, uint32_t maxWidth = 0);
DisplayImage Heatmap(const FloatImage& a, const FloatImage& b, double gain = 4, uint32_t maxWidth = 0);
void SavePng(const std::filesystem::path& path, const DisplayImage& image);
void SavePfm(const std::filesystem::path& path, const FloatImage& image);
FloatImage LoadPfm(const std::filesystem::path& path);
uint64_t NonFinitePixels(const FloatImage& image);
}
