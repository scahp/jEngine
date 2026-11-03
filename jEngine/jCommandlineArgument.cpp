#include "pch.h"
#include "jCommandlineArgument.h"
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cctype>

jCommandLineArgument gCommandLineArgument;

void jCommandLineArgument::Init(int InArgc, const char* const* InArgv)
{
    CommandLineArguments.clear();
    if (!InArgv)
        return;
    for (int i = 1; i < InArgc; ++i)
    {
        if (InArgv[i])
            CommandLineArguments.emplace_back(InArgv[i]);
    }
}

bool jCommandLineArgument::TryParseUInt32(const std::string& InValue, uint32& OutValue)
{
    uint32 parsed = 0;
    const auto result = std::from_chars(InValue.data(), InValue.data() + InValue.size(), parsed);
    if (result.ec != std::errc() || result.ptr != InValue.data() + InValue.size())
        return false;
    OutValue = parsed;
    return true;
}

bool jCommandLineArgument::TryParseNonNegativeFloat(const std::string& InValue, float& OutValue)
{
    if (InValue.empty() || std::isspace(static_cast<unsigned char>(InValue.front())))
        return false;
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(InValue.c_str(), &end);
    if (errno == ERANGE || end != InValue.c_str() + InValue.size() || !std::isfinite(parsed) || parsed < 0.0f)
        return false;
    OutValue = parsed;
    return true;
}
