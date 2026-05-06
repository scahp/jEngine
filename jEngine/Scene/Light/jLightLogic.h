#pragma once

#include "jLight.h"
#include "Math/Matrix.h"
#include "PathTracingDataLoader/json.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

struct jLightLogicContext
{
    float DeltaTime = 0.0f;
    const nlohmann::json* Params = nullptr;
};

using jLightLogicFunc = std::function<void(jLight*, const jLightLogicContext&)>;

class jLightLogicRegistry
{
public:
    static jLightLogicRegistry& Get()
    {
        static jLightLogicRegistry Instance;
        return Instance;
    }

    bool Execute(const std::string& InLogicName, jLight* InLight, const jLightLogicContext& InContext) const
    {
        const std::string normalizedName = NormalizeKey(InLogicName);
        const auto found = LogicMap.find(normalizedName);
        if (found == LogicMap.end())
            return false;

        found->second(InLight, InContext);
        return true;
    }

private:
    jLightLogicRegistry()
    {
        RegisterBuiltinLogics();
    }

    void Register(const std::string& InLogicName, jLightLogicFunc InLogicFunc)
    {
        LogicMap[NormalizeKey(InLogicName)] = std::move(InLogicFunc);
    }

    static std::string NormalizeKey(const std::string& InValue)
    {
        std::string result = InValue;
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return result;
    }

    static bool TryReadVector3(const nlohmann::json& InJson, const char* InKey, Vector& OutValue)
    {
        const auto iter = InJson.find(InKey);
        if (iter == InJson.end() || !iter->is_array() || iter->size() != 3)
            return false;

        if (!(*iter)[0].is_number() || !(*iter)[1].is_number() || !(*iter)[2].is_number())
            return false;

        OutValue = Vector((*iter)[0].get<float>(), (*iter)[1].get<float>(), (*iter)[2].get<float>());
        return true;
    }

    static std::string ReadString(const nlohmann::json& InJson, const char* InKey, const char* InDefault)
    {
        const auto iter = InJson.find(InKey);
        if (iter == InJson.end() || !iter->is_string())
            return InDefault ? InDefault : "";
        return iter->get<std::string>();
    }

    static float ReadFloat(const nlohmann::json& InJson, const char* InKey, float InDefault)
    {
        const auto iter = InJson.find(InKey);
        if (iter == InJson.end() || !iter->is_number())
            return InDefault;
        return iter->get<float>();
    }

    static bool ReadBool(const nlohmann::json& InJson, const char* InKey, bool InDefault)
    {
        const auto iter = InJson.find(InKey);
        if (iter == InJson.end() || !iter->is_boolean())
            return InDefault;
        return iter->get<bool>();
    }

    void RegisterBuiltinLogics()
    {
        Register("rotate_axis", [](jLight* InLight, const jLightLogicContext& InContext)
        {
            if (!InLight)
                return;

            const nlohmann::json emptyParams = nlohmann::json::object();
            const nlohmann::json& params = InContext.Params ? *InContext.Params : emptyParams;

            const float angle = ReadFloat(params, "speed", 1.0f) * InContext.DeltaTime;
            if (IsNearlyZero(angle))
                return;

            Vector axis(0.0f, 1.0f, 0.0f);
            TryReadVector3(params, "axis", axis);
            if (axis.IsNearlyZero())
                return;

            axis = axis.GetNormalize();
            const Matrix rotationMatrix = Matrix::MakeRotate(axis, angle);
            const std::string target = NormalizeKey(ReadString(params, "target", "direction"));
            const bool normalizeResult = ReadBool(params, "normalize", (target == "direction"));

            if (target == "direction")
            {
                if (!InLight->SupportsDirection())
                    return;

                Vector rotatedDirection = rotationMatrix.TransformDirection(InLight->GetDirection());
                if (normalizeResult && !rotatedDirection.IsNearlyZero())
                    rotatedDirection = rotatedDirection.GetNormalize();
                InLight->SetDirection(rotatedDirection);
            }
            else if (target == "position")
            {
                if (!InLight->SupportsPosition())
                    return;

                Vector pivot = Vector::ZeroVector;
                TryReadVector3(params, "pivot", pivot);
                const Vector offset = InLight->GetPosition() - pivot;
                InLight->SetPosition(pivot + rotationMatrix.TransformDirection(offset));
            }
        });
    }

    std::unordered_map<std::string, jLightLogicFunc> LogicMap;
};
