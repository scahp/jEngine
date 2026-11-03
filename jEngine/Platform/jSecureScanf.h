#pragma once

#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

// Narrow compatibility for the formats used by engine/scene loaders: s, scansets,
// unmodified integer/float conversions, field widths, suppression and literals.
// Like Microsoft's CRT, each unsuppressed text destination takes an unsigned
// buffer size. Unsupported conversions/length modifiers fail with EOF/EINVAL.
namespace jPlatformDetail
{
    inline int SecureVScan(const char* Input, const char* Format, va_list& Arguments)
    {
        const auto InvalidFormat = [] { errno = EINVAL; return EOF; };
        if (!Input || !Format)
            return InvalidFormat();

        int Assigned = 0;
        while (*Format)
        {
            if (std::isspace(static_cast<unsigned char>(*Format)))
            {
                while (std::isspace(static_cast<unsigned char>(*Input)))
                    ++Input;
                ++Format;
                continue;
            }
            if (*Format != '%' || Format[1] == '%')
            {
                if (*Format == '%')
                    ++Format;
                if (*Input != *Format)
                    return Assigned || *Input ? Assigned : EOF;
                ++Input;
                ++Format;
                continue;
            }

            const char* Specifier = Format++;
            const bool Suppressed = *Format == '*';
            if (Suppressed)
                ++Format;
            size_t Width = 0;
            const bool HasWidth = std::isdigit(static_cast<unsigned char>(*Format));
            while (std::isdigit(static_cast<unsigned char>(*Format)))
            {
                const unsigned Digit = unsigned(*Format++ - '0');
                if (Width > (size_t(INT_MAX) - Digit) / 10)
                    return InvalidFormat();
                Width = Width * 10 + Digit;
            }
            if (HasWidth && !Width)
                return InvalidFormat();
            if (!HasWidth)
                Width = size_t(-1);

            const char* TypeStart = Format;
            const char Type = *Format;
            if (!Type)
                return InvalidFormat();
            ++Format;
            if (Type == '[')
            {
                if (*Format == '^')
                    ++Format;
                if (*Format == ']')
                    ++Format;
                const char* End = std::strchr(Format, ']');
                if (!End)
                    return InvalidFormat();
                Format = End + 1;
            }

            if (Type == 's' || Type == '[')
            {
                char* Destination = nullptr;
                unsigned Capacity = 0;
                if (!Suppressed)
                {
                    Destination = va_arg(Arguments, char*);
                    Capacity = va_arg(Arguments, unsigned);
                    if (!Destination || !Capacity)
                        return InvalidFormat();
                }
                if (Type == 's')
                    while (std::isspace(static_cast<unsigned char>(*Input)))
                        ++Input;

                // Let libc interpret scanset ranges/negation; the one-byte probe
                // is always bounded, even when the original token is oversized.
                const std::string ScanSet = Type == '[' ? "%1" + std::string(TypeStart, Format) : "";
                size_t Count = 0;
                while (Count < Width && Input[Count])
                {
                    char Probe[2];
                    const bool Matches = Type == 's'
                        ? !std::isspace(static_cast<unsigned char>(Input[Count]))
                        : std::sscanf(Input + Count, ScanSet.c_str(), Probe) == 1;
                    if (!Matches)
                        break;
                    ++Count;
                }
                if (!Count)
                    return Assigned || *Input ? Assigned : EOF;
                if (!Suppressed)
                {
                    if (Count >= Capacity)
                        return Assigned;
                    std::memcpy(Destination, Input, Count);
                    Destination[Count] = '\0';
                    ++Assigned;
                }
                Input += Count;
                continue;
            }

            if (!std::strchr("diouxXaAeEfFgG", Type))
                return InvalidFormat();
            const std::string ScanFormat = std::string(Specifier, Format) + "%n";
            int Consumed = -1;
            int Result = 0;
            const auto Read = [&](auto* Destination)
            {
                if (!Destination)
                    return EOF;
                return std::sscanf(Input, ScanFormat.c_str(), Destination, &Consumed);
            };
            if (Suppressed)
                Result = std::sscanf(Input, ScanFormat.c_str(), &Consumed);
            else if (Type == 'd' || Type == 'i')
                Result = Read(va_arg(Arguments, int*));
            else if (std::strchr("ouxX", Type))
                Result = Read(va_arg(Arguments, unsigned*));
            else
                Result = Read(va_arg(Arguments, float*));
            if (Consumed < 0)
                return Assigned ? Assigned : (Result == EOF ? EOF : 0);
            Input += Consumed;
            if (!Suppressed)
                ++Assigned;
        }
        return Assigned;
    }
}

inline int sscanf_s(const char* Input, const char* Format, ...)
{
    va_list Arguments;
    va_start(Arguments, Format);
    struct EndArguments
    {
        va_list& Arguments;
        ~EndArguments() { va_end(Arguments); }
    } End{Arguments};
    return jPlatformDetail::SecureVScan(Input, Format, Arguments);
}
