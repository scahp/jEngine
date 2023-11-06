#pragma once
#include "Math/Vector.h"

struct jCommandLineArgument
{
public:
    void Init(const char* InCommandLine)
    {
        CommandLineArguments = GetTokenizedResult(InCommandLine);
    }

    bool HasArgument(const std::string& InToken) const
    {
        for (int32 i = 0; i < (int32)CommandLineArguments.size(); ++i)
        {
            if (0 == _stricmp(CommandLineArguments[i].c_str(), InToken.c_str()))
            {
                return true;
            }
        }
        return false;
    }

    const std::vector<std::string>& GetCommandLineArguments() const { return CommandLineArguments; }

private:
    std::vector<std::string> GetTokenizedResult(const char* InString, const char* InToken = " ")
    {
        std::vector<std::string> Result;
        if (!InString || !InToken)
            return Result;

        std::string TemporaryString = InString;		// strtok_s 는 입력 string 을 변경하기 때문에 임시 문자열 필요함.
        char* Context = NULL;
        char* Token = strtok_s(TemporaryString.data(), InToken, &Context); // context에는 분리된 후 남은 문자열이 들어간다.

        do
        {
            if (Token)
            {
                Result.push_back(Token);
            }
            Token = strtok_s(NULL, InToken, &Context);
        } while (Token);

        return Result;
    }

    std::vector<std::string> CommandLineArguments;
};

extern jCommandLineArgument gCommandLineArgument;