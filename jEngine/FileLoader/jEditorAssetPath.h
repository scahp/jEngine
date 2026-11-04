#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <stdexcept>

// UI, authored JSON and asset request strings use UTF-8 on every platform.
// Keep native filesystem paths at OS/stream calls; narrow path.string() uses
// the Windows ANSI codepage and is unsuitable for these persistence boundaries.
namespace jEditorAssetPath
{
inline bool IsValidUtf8(std::string_view text)
{
    for (size_t i = 0; i < text.size();)
    {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) continue;
        unsigned value = 0, count = 0, minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { value = lead & 0x1f; count = 1; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { value = lead & 0x0f; count = 2; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { value = lead & 7; count = 3; minimum = 0x10000; }
        else return false;
        if (count > text.size() - i) return false;
        while (count--)
        {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
inline std::filesystem::path FromUtf8(std::string_view text)
{
    if (!IsValidUtf8(text)) throw std::invalid_argument("Asset path must be valid UTF-8");
    if (text.empty()) return {};
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
inline std::string ToUtf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
}
