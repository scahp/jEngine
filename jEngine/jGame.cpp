#include "pch.h"
#include "jGame.h"
#include "Math/Vector.h"
#include "Scene/jCamera.h"
#include "Scene/jObject.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/Light/jLightLogic.h"
#include "Scene/jRenderObject.h"
#include "Profiler/jPerformanceProfile.h"
#include "Renderer/jRenderer.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "FileLoader/jModelLoader.h"
#include "Scene/jMeshObject.h"
#include "FileLoader/jImageFileLoader.h"
#include "Renderer/jSceneRenderTargets.h"    // temp
#include "dxcapi.h"
#include "RHI/jRaytracingScene.h"
#include "Renderer/jDirectionalLightDrawCommandGenerator.h"
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include "Code/Engine/ConsoleVariables/jConsoleVariable.h"
#include "Renderer/jPointLightDrawCommandGenerator.h"
#include "Renderer/jSpotLightDrawCommandGenerator.h"
#include "PathTracingDataLoader/jPathTracingData.h"
#include "PathTracingDataLoader/PathTracingDataLoader.h"
#include "PathTracingDataLoader/GLTFLoader.h"
#include "Renderer/jRenderer_PathTracing.h"
#include "FileLoader/jFile.h"
#include "PathTracingDataLoader/json.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#endif

jRHI* g_rhi = nullptr;
namespace
{
	const char* GetSceneRenderPipelineNameInternal(jGame::ESceneRenderPipeline InRenderPipeline)
	{
		switch (InRenderPipeline)
		{
		case jGame::ESceneRenderPipeline::Forward: return "Forward";
		case jGame::ESceneRenderPipeline::PathTracing: return "PathTracing";
		case jGame::ESceneRenderPipeline::Deferred:
		default:
			return "Deferred";
		}
	}

	const char* GetSceneLoaderNameInternal(jGame::ESceneLoader InLoader)
	{
		switch (InLoader)
		{
		case jGame::ESceneLoader::Recommended: return "Recommended";
		case jGame::ESceneLoader::Model: return "Model";
		case jGame::ESceneLoader::PathTracing: return "PathTracing";
		default: return "Unknown";
		}
	}

	jGame::ESceneRenderPipeline GetDefaultSceneRenderPipeline(const std::filesystem::path& InPath)
	{
		std::string extension = InPath.extension().generic_string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (extension == ".scene")
			return jGame::ESceneRenderPipeline::PathTracing;
		return jGame::ESceneRenderPipeline::Deferred;
	}

	jGame::ESceneLoader GetRecommendedSceneLoader(const std::filesystem::path& InPath)
	{
		std::string extension = InPath.extension().generic_string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (extension == ".scene")
			return jGame::ESceneLoader::PathTracing;
		return jGame::ESceneLoader::Model;
	}

	bool TryParseSceneRenderPipeline(const std::string& InValue, jGame::ESceneRenderPipeline& OutRenderPipeline)
	{
		std::string normalized = InValue;
		normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), normalized.end());
		std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (normalized == "forward")
		{
			OutRenderPipeline = jGame::ESceneRenderPipeline::Forward;
			return true;
		}
		if (normalized == "pathtracing" || normalized == "path_tracing")
		{
			OutRenderPipeline = jGame::ESceneRenderPipeline::PathTracing;
			return true;
		}
		if (normalized == "deferred")
		{
			OutRenderPipeline = jGame::ESceneRenderPipeline::Deferred;
			return true;
		}
		return false;
	}

	bool TryParseSceneLoader(const std::string& InValue, jGame::ESceneLoader& OutLoader)
	{
		std::string normalized = InValue;
		normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), normalized.end());
		std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (normalized == "recommended" || normalized == "default" || normalized == "auto")
		{
			OutLoader = jGame::ESceneLoader::Recommended;
			return true;
		}
		if (normalized == "model")
		{
			OutLoader = jGame::ESceneLoader::Model;
			return true;
		}
		if (normalized == "pathtracing" || normalized == "path_tracing")
		{
			OutLoader = jGame::ESceneLoader::PathTracing;
			return true;
		}
		return false;
	}

	jGame::ESceneLoader ResolveSceneLoader(const jGame::jLoadableSceneDesc& InSceneDesc)
	{
		return (InSceneDesc.SelectedLoader == jGame::ESceneLoader::Recommended)
			? InSceneDesc.RecommendedLoader
			: InSceneDesc.SelectedLoader;
	}

	std::string TrimString(const std::string& InValue)
	{
		size_t begin = 0;
		while (begin < InValue.size() && std::isspace(static_cast<unsigned char>(InValue[begin])) != 0)
			++begin;

		size_t end = InValue.size();
		while (end > begin && std::isspace(static_cast<unsigned char>(InValue[end - 1])) != 0)
			--end;

		return InValue.substr(begin, end - begin);
	}

	std::string ToLowerString(const std::string& InValue)
	{
		std::string result = InValue;
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return result;
	}

	std::string UnquoteString(const std::string& InValue)
	{
		if (InValue.size() >= 2 && InValue.front() == '"' && InValue.back() == '"')
			return InValue.substr(1, InValue.size() - 2);
		return InValue;
	}

	bool TryGetIniSectionName(const std::string& InLine, std::string& OutSectionName)
	{
		const std::string trimmedLine = TrimString(InLine);
		if (trimmedLine.size() >= 2 && trimmedLine.front() == '[' && trimmedLine.back() == ']')
		{
			OutSectionName = ToLowerString(TrimString(trimmedLine.substr(1, trimmedLine.size() - 2)));
			return true;
		}
		return false;
	}

	bool IsLoadablePathTracingSceneFile(const std::filesystem::path& InPath)
	{
		std::string extension = InPath.extension().generic_string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return (extension == ".scene" || extension == ".gltf" || extension == ".glb");
	}

	std::string MakeSceneId(const std::filesystem::path& InRootPath, const std::filesystem::path& InFilePath)
	{
		std::error_code errorCode;
		const auto relativePath = std::filesystem::relative(InFilePath, InRootPath, errorCode);
		return (errorCode ? InFilePath.lexically_normal() : relativePath.lexically_normal()).generic_string();
	}

	std::filesystem::path GetSceneObjectConfigPath(const std::filesystem::path& InScenePath)
	{
		return InScenePath.parent_path() / "objects.json";
	}

	struct jSceneCameraPlacement
	{
		bool IsValid = false;
		Vector Position = Vector::ZeroVector;
		Vector Target = Vector::ZeroVector;
		Vector Up = Vector(0.0f, 1.0f, 0.0f);
		float FovRad = DegreeToRadian(45.0f);
		float Near = 10.0f;
		float Far = 5000.0f;
	};

	struct jSceneDirectionalLightPlacement
	{
		bool IsValid = false;
		Vector Direction = Vector(0.0f, -1.0f, 0.0f);
		Vector Color = Vector(1.0f);
	};

	struct jScenePointLightPlacement
	{
		bool IsValid = false;
		Vector Position = Vector::ZeroVector;
		Vector Color = Vector(1.0f);
		float MaxDistance = 150.0f;
	};

	struct jSceneSpotLightPlacement
	{
		bool IsValid = false;
		Vector Position = Vector::ZeroVector;
		Vector Direction = Vector(0.0f, -1.0f, 0.0f);
		Vector Color = Vector(1.0f);
		float MaxDistance = 200.0f;
		float PenumbraRadian = 0.35f;
		float UmbraRadian = 0.5f;
	};

	struct jScenePlacementPreset
	{
		enum class EObjectType
		{
			Sphere = 0,
			Quad,
			Triangle,
			Cube,
			Capsule,
			Cone,
			Cylinder,
		};

		struct jObjectPlacement
		{
			bool IsValid = false;
			EObjectType Type = EObjectType::Sphere;
			Vector Position = Vector::ZeroVector;
			Vector Size = Vector::OneVector;
			Vector Scale = Vector::OneVector;
			Vector4 Color = Vector4::OneVector;
			float Radius = 1.0f;
			float Height = 1.0f;
			int32 Slices = 20;
			int32 Stacks = 20;
		};

		struct jLightPlacement
		{
			bool IsValid = false;
			ELightType Type = ELightType::POINT;
			Vector Position = Vector::ZeroVector;
			Vector Direction = Vector(0.0f, -1.0f, 0.0f);
			Vector Color = Vector(1.0f);
			float MaxDistance = 150.0f;
			float PenumbraRadian = 0.35f;
			float UmbraRadian = 0.5f;
			std::string LogicName;
			nlohmann::json LogicParams = nlohmann::json::object();
		};

		bool IsValid = false;
		jSceneCameraPlacement Camera;
		jSceneDirectionalLightPlacement DirectionalLight;
		jScenePointLightPlacement PointLight;
		jSceneSpotLightPlacement SpotLight;
		std::vector<jObjectPlacement> Objects;
		std::vector<jLightPlacement> Lights;
	};

	bool TryParseSceneObjectType(const std::string& InValue, jScenePlacementPreset::EObjectType& OutType)
	{
		std::string normalized = InValue;
		std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (normalized == "sphere")
		{
			OutType = jScenePlacementPreset::EObjectType::Sphere;
			return true;
		}
		if (normalized == "quad")
		{
			OutType = jScenePlacementPreset::EObjectType::Quad;
			return true;
		}
		if (normalized == "triangle")
		{
			OutType = jScenePlacementPreset::EObjectType::Triangle;
			return true;
		}
		if (normalized == "cube")
		{
			OutType = jScenePlacementPreset::EObjectType::Cube;
			return true;
		}
		if (normalized == "capsule")
		{
			OutType = jScenePlacementPreset::EObjectType::Capsule;
			return true;
		}
		if (normalized == "cone")
		{
			OutType = jScenePlacementPreset::EObjectType::Cone;
			return true;
		}
		if (normalized == "cylinder")
		{
			OutType = jScenePlacementPreset::EObjectType::Cylinder;
			return true;
		}
		return false;
	}

	bool TryParseSceneLightType(const std::string& InValue, ELightType& OutType)
	{
		std::string normalized = InValue;
		std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if (normalized == "directional")
		{
			OutType = ELightType::DIRECTIONAL;
			return true;
		}
		if (normalized == "point")
		{
			OutType = ELightType::POINT;
			return true;
		}
		if (normalized == "spot")
		{
			OutType = ELightType::SPOT;
			return true;
		}
		return false;
	}

	bool TryReadVector3(const nlohmann::json& InValue, Vector& OutValue)
	{
		if (!InValue.is_array() || InValue.size() != 3)
			return false;

		if (!InValue[0].is_number() || !InValue[1].is_number() || !InValue[2].is_number())
			return false;

		OutValue = Vector(InValue[0].get<float>(), InValue[1].get<float>(), InValue[2].get<float>());
		return true;
	}

	bool TryReadVector4(const nlohmann::json& InValue, Vector4& OutValue)
	{
		if (!InValue.is_array() || InValue.size() != 4)
			return false;

		if (!InValue[0].is_number() || !InValue[1].is_number() || !InValue[2].is_number() || !InValue[3].is_number())
			return false;

		OutValue = Vector4(InValue[0].get<float>(), InValue[1].get<float>(), InValue[2].get<float>(), InValue[3].get<float>());
		return true;
	}

	bool TryParseSceneCameraPlacement(const nlohmann::json& InValue, jSceneCameraPlacement& OutPlacement)
	{
		if (!InValue.is_object())
			return false;

		if (!TryReadVector3(InValue.value("position", nlohmann::json::array()), OutPlacement.Position)
			|| !TryReadVector3(InValue.value("target", nlohmann::json::array()), OutPlacement.Target))
		{
			return false;
		}

		if (!TryReadVector3(InValue.value("up", nlohmann::json::array({ 0.0f, 1.0f, 0.0f })), OutPlacement.Up))
			return false;

		if (InValue.contains("fovDegree"))
			OutPlacement.FovRad = DegreeToRadian(InValue.value("fovDegree", 45.0f));
		else
			OutPlacement.FovRad = InValue.value("fovRad", DegreeToRadian(45.0f));
		OutPlacement.Near = InValue.value("near", 10.0f);
		OutPlacement.Far = InValue.value("far", 5000.0f);
		OutPlacement.IsValid = true;
		return true;
	}

	bool TryParseSceneObjectPlacement(const nlohmann::json& InValue, jScenePlacementPreset::jObjectPlacement& OutPlacement)
	{
		if (!InValue.is_object())
			return false;

		const auto typeIter = InValue.find("type");
		if (typeIter == InValue.end() || !typeIter->is_string())
			return false;

		if (!TryParseSceneObjectType(typeIter->get<std::string>(), OutPlacement.Type))
			return false;

		switch (OutPlacement.Type)
		{
		case jScenePlacementPreset::EObjectType::Sphere:
		{
			if (!TryReadVector3(InValue.value("position", nlohmann::json::array()), OutPlacement.Position)
				|| !TryReadVector3(InValue.value("scale", nlohmann::json::array({ 1.0f, 1.0f, 1.0f })), OutPlacement.Scale)
				|| !TryReadVector4(InValue.value("color", nlohmann::json::array({ 1.0f, 1.0f, 1.0f, 1.0f })), OutPlacement.Color))
			{
				return false;
			}
			OutPlacement.Radius = InValue.value("radius", 1.0f);
			OutPlacement.Slices = Max(3, InValue.value("slices", 20));
			OutPlacement.Stacks = Max(2, InValue.value("stacks", 20));
			break;
		}
		case jScenePlacementPreset::EObjectType::Quad:
		case jScenePlacementPreset::EObjectType::Triangle:
		case jScenePlacementPreset::EObjectType::Cube:
		{
			if (!TryReadVector3(InValue.value("position", nlohmann::json::array()), OutPlacement.Position)
				|| !TryReadVector3(InValue.value("size", nlohmann::json::array()), OutPlacement.Size)
				|| !TryReadVector3(InValue.value("scale", nlohmann::json::array({ 1.0f, 1.0f, 1.0f })), OutPlacement.Scale)
				|| !TryReadVector4(InValue.value("color", nlohmann::json::array({ 1.0f, 1.0f, 1.0f, 1.0f })), OutPlacement.Color))
			{
				return false;
			}
			break;
		}
		case jScenePlacementPreset::EObjectType::Capsule:
		case jScenePlacementPreset::EObjectType::Cone:
		case jScenePlacementPreset::EObjectType::Cylinder:
		{
			if (!TryReadVector3(InValue.value("position", nlohmann::json::array()), OutPlacement.Position)
				|| !TryReadVector3(InValue.value("scale", nlohmann::json::array({ 1.0f, 1.0f, 1.0f })), OutPlacement.Scale)
				|| !TryReadVector4(InValue.value("color", nlohmann::json::array({ 1.0f, 1.0f, 1.0f, 1.0f })), OutPlacement.Color))
			{
				return false;
			}
			OutPlacement.Height = InValue.value("height", 1.0f);
			OutPlacement.Radius = InValue.value("radius", 1.0f);
			OutPlacement.Slices = Max(3, InValue.value("slices", 20));
			break;
		}
		default:
			return false;
		}

		OutPlacement.IsValid = true;
		return true;
	}

	bool TryParseSceneLightPlacement(const nlohmann::json& InValue, jScenePlacementPreset::jLightPlacement& OutPlacement)
	{
		if (!InValue.is_object())
			return false;

		const auto typeIter = InValue.find("type");
		if (typeIter == InValue.end() || !typeIter->is_string())
			return false;

		if (!TryParseSceneLightType(typeIter->get<std::string>(), OutPlacement.Type))
			return false;

		if (!TryReadVector3(InValue.value("color", nlohmann::json::array({ 1.0f, 1.0f, 1.0f })), OutPlacement.Color))
			return false;

		switch (OutPlacement.Type)
		{
		case ELightType::DIRECTIONAL:
			if (!TryReadVector3(InValue.value("direction", nlohmann::json::array()), OutPlacement.Direction))
				return false;
			break;
		case ELightType::POINT:
			if (!TryReadVector3(InValue.value("position", nlohmann::json::array()), OutPlacement.Position))
				return false;
			OutPlacement.MaxDistance = InValue.value("maxDistance", 150.0f);
			break;
		case ELightType::SPOT:
			if (!TryReadVector3(InValue.value("position", nlohmann::json::array()), OutPlacement.Position)
				|| !TryReadVector3(InValue.value("direction", nlohmann::json::array()), OutPlacement.Direction))
			{
				return false;
			}
			OutPlacement.MaxDistance = InValue.value("maxDistance", 200.0f);
			OutPlacement.PenumbraRadian = InValue.value("penumbraRadian", InValue.value("penumbra", 0.35f));
			OutPlacement.UmbraRadian = InValue.value("umbraRadian", InValue.value("umbra", 0.5f));
			break;
		default:
			return false;
		}

		const auto logicIter = InValue.find("logic");
		if (logicIter != InValue.end() && logicIter->is_object())
		{
			const auto nameIter = logicIter->find("name");
			if (nameIter != logicIter->end() && nameIter->is_string())
			{
				OutPlacement.LogicName = nameIter->get<std::string>();
				const auto paramsIter = logicIter->find("params");
				if (paramsIter != logicIter->end() && paramsIter->is_object())
					OutPlacement.LogicParams = *paramsIter;
			}
		}

		OutPlacement.IsValid = true;
		return true;
	}

	void ApplySceneObjectConfig(const std::filesystem::path& InScenePath, jGame::jLoadableSceneDesc* InOutSceneDesc = nullptr, jScenePlacementPreset* InOutPreset = nullptr)
	{
		const std::filesystem::path objectConfigPath = GetSceneObjectConfigPath(InScenePath);
		if (!std::filesystem::exists(objectConfigPath))
			return;

		std::ifstream objectConfigFile(objectConfigPath);
		if (!objectConfigFile.is_open())
			return;

		std::stringstream buffer;
		buffer << objectConfigFile.rdbuf();
		const nlohmann::json objectConfigJson = nlohmann::json::parse(buffer.str(), nullptr, false);
		if (objectConfigJson.is_discarded() || !objectConfigJson.is_object())
			return;

		if (InOutSceneDesc)
		{
			const auto rendererIter = objectConfigJson.find("renderer");
			if (rendererIter != objectConfigJson.end() && rendererIter->is_string())
			{
				jGame::ESceneRenderPipeline renderPipeline = InOutSceneDesc->RenderPipeline;
				if (TryParseSceneRenderPipeline(rendererIter->get<std::string>(), renderPipeline))
					InOutSceneDesc->RenderPipeline = renderPipeline;
			}

			const auto loaderIter = objectConfigJson.find("loader");
			if (loaderIter != objectConfigJson.end() && loaderIter->is_string())
			{
				jGame::ESceneLoader loader = InOutSceneDesc->RecommendedLoader;
				if (TryParseSceneLoader(loaderIter->get<std::string>(), loader) && loader != jGame::ESceneLoader::Recommended)
					InOutSceneDesc->RecommendedLoader = loader;
			}
		}

		if (InOutPreset)
		{
			const auto cameraIter = objectConfigJson.find("camera");
			if (cameraIter != objectConfigJson.end())
			{
				jSceneCameraPlacement cameraPlacement;
				if (TryParseSceneCameraPlacement(*cameraIter, cameraPlacement))
				{
					InOutPreset->Camera = cameraPlacement;
					InOutPreset->IsValid = true;
				}
			}

			const auto objectsIter = objectConfigJson.find("objects");
			if (objectsIter != objectConfigJson.end() && objectsIter->is_array())
			{
				for (const auto& objectJson : *objectsIter)
				{
					jScenePlacementPreset::jObjectPlacement objectPlacement;
					if (TryParseSceneObjectPlacement(objectJson, objectPlacement))
					{
						InOutPreset->Objects.push_back(objectPlacement);
						InOutPreset->IsValid = true;
					}
				}
			}

			const auto lightsIter = objectConfigJson.find("lights");
			if (lightsIter != objectConfigJson.end() && lightsIter->is_array())
			{
				for (const auto& lightJson : *lightsIter)
				{
					jScenePlacementPreset::jLightPlacement lightPlacement;
					if (TryParseSceneLightPlacement(lightJson, lightPlacement))
					{
						InOutPreset->Lights.push_back(lightPlacement);
						InOutPreset->IsValid = true;
					}
				}
			}
		}
	}

	jScenePlacementPreset GetScenePlacementPreset(const jGame::jLoadableSceneDesc& InSceneDesc)
	{
		jScenePlacementPreset preset;
		ApplySceneObjectConfig(std::filesystem::path(InSceneDesc.FilePath), nullptr, &preset);

		return preset;
	}
}

jGame::jGame()
{
}

jGame::~jGame()
{
}

const std::vector<jGame::jLoadableSceneDesc>& jGame::GetLoadablePathTracingScenes() const
{
	return PathTracingSceneBrowser.Scenes;
}

int32 jGame::GetSelectedPathTracingSceneIndex() const
{
	return PathTracingSceneBrowser.SelectedIndex;
}

int32 jGame::GetActivePathTracingSceneIndex() const
{
	return PathTracingSceneBrowser.ActiveIndex;
}

const char* jGame::GetSelectedPathTracingSceneName() const
{
	const int32 selectedIndex = PathTracingSceneBrowser.SelectedIndex;
	if (selectedIndex >= 0 && selectedIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		return PathTracingSceneBrowser.Scenes[selectedIndex].DisplayName.c_str();

	return "None";
}

const char* jGame::GetActivePathTracingSceneName() const
{
	const int32 activeIndex = PathTracingSceneBrowser.ActiveIndex;
	if (activeIndex >= 0 && activeIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		return PathTracingSceneBrowser.Scenes[activeIndex].DisplayName.c_str();

	return "None";
}

const char* jGame::GetSceneRenderPipelineName(int32 InIndex) const
{
	if (InIndex < 0 || InIndex >= (int32)PathTracingSceneBrowser.Scenes.size())
		return "Unknown";

	return GetSceneRenderPipelineNameInternal(PathTracingSceneBrowser.Scenes[InIndex].RenderPipeline);
}

const char* jGame::GetActiveSceneRenderPipelineName() const
{
	return GetSceneRenderPipelineNameInternal(PathTracingSceneBrowser.ActiveRenderPipeline);
}

const char* jGame::GetSceneRecommendedLoaderName(int32 InIndex) const
{
	if (InIndex < 0 || InIndex >= (int32)PathTracingSceneBrowser.Scenes.size())
		return "Unknown";

	return GetSceneLoaderNameInternal(PathTracingSceneBrowser.Scenes[InIndex].RecommendedLoader);
}

const char* jGame::GetSelectedPathTracingSceneLoaderName() const
{
	const int32 selectedIndex = PathTracingSceneBrowser.SelectedIndex;
	if (selectedIndex < 0 || selectedIndex >= (int32)PathTracingSceneBrowser.Scenes.size())
		return "None";

	return GetSceneLoaderNameInternal(PathTracingSceneBrowser.Scenes[selectedIndex].SelectedLoader);
}

const char* jGame::GetActivePathTracingSceneLoaderName() const
{
	return GetSceneLoaderNameInternal(PathTracingSceneBrowser.ActiveLoader);
}

jGame::ESceneLoader jGame::GetSelectedPathTracingSceneLoader() const
{
	const int32 selectedIndex = PathTracingSceneBrowser.SelectedIndex;
	if (selectedIndex < 0 || selectedIndex >= (int32)PathTracingSceneBrowser.Scenes.size())
		return ESceneLoader::Recommended;

	return PathTracingSceneBrowser.Scenes[selectedIndex].SelectedLoader;
}

bool jGame::IsUsingPathTracingRenderer() const
{
	return PathTracingSceneBrowser.ActiveRenderPipeline == ESceneRenderPipeline::PathTracing;
}

void jGame::SetSelectedPathTracingSceneIndex(int32 InIndex)
{
	if (InIndex >= 0 && InIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		PathTracingSceneBrowser.SelectedIndex = InIndex;
}

void jGame::SetSelectedPathTracingSceneLoader(ESceneLoader InLoader)
{
	const int32 selectedIndex = PathTracingSceneBrowser.SelectedIndex;
	if (selectedIndex >= 0 && selectedIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		PathTracingSceneBrowser.Scenes[selectedIndex].SelectedLoader = InLoader;
}

void jGame::RequestLoadSelectedPathTracingScene()
{
	if (CanLoadSelectedPathTracingScene())
		PathTracingSceneBrowser.PendingLoadIndex = PathTracingSceneBrowser.SelectedIndex;
}

bool jGame::CanLoadSelectedPathTracingScene() const
{
	const int32 selectedIndex = PathTracingSceneBrowser.SelectedIndex;
	return (selectedIndex >= 0
		&& selectedIndex < (int32)PathTracingSceneBrowser.Scenes.size()
		&& (selectedIndex != PathTracingSceneBrowser.ActiveIndex
			|| ResolveSceneLoader(PathTracingSceneBrowser.Scenes[selectedIndex]) != PathTracingSceneBrowser.ActiveLoader));
}

void jGame::InitializePathTracingSceneBrowser()
{
	if (PathTracingSceneBrowser.Initialized)
		return;

	PathTracingSceneBrowser.Initialized = true;
	RefreshPathTracingSceneCatalog();
	LoadPathTracingSceneBrowserSettings();

	if (!PathTracingSceneBrowser.LastLoadedSceneId.empty())
	{
		const int32 cachedSceneIndex = FindPathTracingSceneIndexById(PathTracingSceneBrowser.LastLoadedSceneId);
		if (cachedSceneIndex >= 0 && cachedSceneIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		{
			PathTracingSceneBrowser.Scenes[cachedSceneIndex].SelectedLoader = PathTracingSceneBrowser.LastLoadedLoader;
			PathTracingSceneBrowser.SelectedIndex = cachedSceneIndex;
			PathTracingSceneBrowser.PendingLoadIndex = cachedSceneIndex;
		}
	}
}

void jGame::RefreshPathTracingSceneCatalog()
{
	std::string previousSelectedSceneId;
	std::string previousActiveSceneId;
	std::string previousPendingSceneId;
	std::unordered_map<std::string, ESceneLoader> previousSelectedLoaders;

	if (PathTracingSceneBrowser.SelectedIndex >= 0 && PathTracingSceneBrowser.SelectedIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		previousSelectedSceneId = PathTracingSceneBrowser.Scenes[PathTracingSceneBrowser.SelectedIndex].SceneId;
	if (PathTracingSceneBrowser.ActiveIndex >= 0 && PathTracingSceneBrowser.ActiveIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		previousActiveSceneId = PathTracingSceneBrowser.Scenes[PathTracingSceneBrowser.ActiveIndex].SceneId;
	if (PathTracingSceneBrowser.PendingLoadIndex >= 0 && PathTracingSceneBrowser.PendingLoadIndex < (int32)PathTracingSceneBrowser.Scenes.size())
		previousPendingSceneId = PathTracingSceneBrowser.Scenes[PathTracingSceneBrowser.PendingLoadIndex].SceneId;

	for (const auto& sceneDesc : PathTracingSceneBrowser.Scenes)
	{
		previousSelectedLoaders[sceneDesc.SceneId] = sceneDesc.SelectedLoader;
	}

	PathTracingSceneBrowser.Scenes.clear();

	const std::filesystem::path rootPath(PathTracingSceneBrowser.RootFolder);
	if (std::filesystem::exists(rootPath))
	{
		for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath))
		{
			if (!entry.is_regular_file() || !IsLoadablePathTracingSceneFile(entry.path()))
				continue;

			jLoadableSceneDesc sceneDesc;
			sceneDesc.SceneId = MakeSceneId(rootPath, entry.path());
			sceneDesc.FilePath = entry.path().generic_string();
			sceneDesc.DisplayName = sceneDesc.SceneId;
			sceneDesc.Extension = entry.path().extension().generic_string();
			sceneDesc.RenderPipeline = GetDefaultSceneRenderPipeline(entry.path());
			sceneDesc.RecommendedLoader = GetRecommendedSceneLoader(entry.path());
			sceneDesc.SelectedLoader = ESceneLoader::Recommended;
			ApplySceneObjectConfig(entry.path(), &sceneDesc, nullptr);
			const auto previousLoaderIter = previousSelectedLoaders.find(sceneDesc.SceneId);
			if (previousLoaderIter != previousSelectedLoaders.end())
				sceneDesc.SelectedLoader = previousLoaderIter->second;
			PathTracingSceneBrowser.Scenes.push_back(std::move(sceneDesc));
		}
	}

	std::sort(PathTracingSceneBrowser.Scenes.begin(), PathTracingSceneBrowser.Scenes.end(),
		[](const jLoadableSceneDesc& lhs, const jLoadableSceneDesc& rhs)
		{
			return lhs.SceneId < rhs.SceneId;
		});

	const auto findSceneIndexById = [this](const std::string& InSceneId) -> int32
	{
		if (InSceneId.empty())
			return -1;

		for (int32 i = 0; i < (int32)PathTracingSceneBrowser.Scenes.size(); ++i)
		{
			if (PathTracingSceneBrowser.Scenes[i].SceneId == InSceneId)
				return i;
		}
		return -1;
	};

	PathTracingSceneBrowser.SelectedIndex = findSceneIndexById(previousSelectedSceneId);
	PathTracingSceneBrowser.ActiveIndex = findSceneIndexById(previousActiveSceneId);
	PathTracingSceneBrowser.PendingLoadIndex = findSceneIndexById(previousPendingSceneId);
	if (PathTracingSceneBrowser.ActiveIndex >= 0)
	{
		PathTracingSceneBrowser.ActiveRenderPipeline = PathTracingSceneBrowser.Scenes[PathTracingSceneBrowser.ActiveIndex].RenderPipeline;
		PathTracingSceneBrowser.ActiveLoader = ResolveSceneLoader(PathTracingSceneBrowser.Scenes[PathTracingSceneBrowser.ActiveIndex]);
	}
	else
	{
		PathTracingSceneBrowser.ActiveLoader = ESceneLoader::Model;
	}

	if (PathTracingSceneBrowser.SelectedIndex < 0 && !PathTracingSceneBrowser.Scenes.empty())
		PathTracingSceneBrowser.SelectedIndex = 0;
}

void jGame::LoadPathTracingSceneBrowserSettings()
{
	PathTracingSceneBrowser.LastLoadedSceneId.clear();
	PathTracingSceneBrowser.LastLoadedLoader = ESceneLoader::Model;

	std::ifstream inputFile(PathTracingSceneBrowser.SettingsFile);
	if (!inputFile.is_open())
		return;

	bool isSceneBrowserSection = false;
	std::string line;
	while (std::getline(inputFile, line))
	{
		const std::string trimmedLine = TrimString(line);
		if (trimmedLine.empty() || trimmedLine[0] == ';' || trimmedLine[0] == '#')
			continue;

		std::string sectionName;
		if (TryGetIniSectionName(trimmedLine, sectionName))
		{
			isSceneBrowserSection = (sectionName == "scenebrowser");
			continue;
		}

		if (!isSceneBrowserSection)
			continue;

		const size_t separatorPos = trimmedLine.find('=');
		if (separatorPos == std::string::npos)
			continue;

		const std::string key = ToLowerString(TrimString(trimmedLine.substr(0, separatorPos)));
		const std::string value = UnquoteString(TrimString(trimmedLine.substr(separatorPos + 1)));
		if (key == "lastloadedsceneid")
		{
			PathTracingSceneBrowser.LastLoadedSceneId = value;
		}
		else if (key == "lastloadedloader")
		{
			ESceneLoader loader = PathTracingSceneBrowser.LastLoadedLoader;
			if (TryParseSceneLoader(value, loader))
				PathTracingSceneBrowser.LastLoadedLoader = loader;
		}
	}
}

void jGame::SavePathTracingSceneBrowserSettings() const
{
	std::vector<std::string> preservedLines;
	std::ifstream inputFile(PathTracingSceneBrowser.SettingsFile);
	if (inputFile.is_open())
	{
		bool isFilteredSection = false;
		std::string line;
		while (std::getline(inputFile, line))
		{
			std::string sectionName;
			if (TryGetIniSectionName(line, sectionName))
			{
				if (sectionName == "scenebrowser" || sectionName == "scenestartpoints")
				{
					isFilteredSection = true;
					continue;
				}
				isFilteredSection = false;
			}

			if (!isFilteredSection)
				preservedLines.push_back(line);
		}
	}

	const std::filesystem::path settingsPath(PathTracingSceneBrowser.SettingsFile);
	if (settingsPath.has_parent_path())
		std::filesystem::create_directories(settingsPath.parent_path());

	std::ofstream outputFile(PathTracingSceneBrowser.SettingsFile, std::ios::trunc);
	if (!outputFile.is_open())
		return;

	for (const std::string& line : preservedLines)
		outputFile << line << '\n';

	if (!preservedLines.empty() && !preservedLines.back().empty())
		outputFile << '\n';

	outputFile << "[SceneBrowser]\n";
	outputFile << "LastLoadedSceneId=" << std::quoted(PathTracingSceneBrowser.LastLoadedSceneId) << '\n';
	outputFile << "LastLoadedLoader=" << GetSceneLoaderNameInternal(PathTracingSceneBrowser.LastLoadedLoader) << '\n';
}

void jGame::ClearSceneBrowserLoadedObjects()
{
	for (jObject* object : SceneBrowserLoadedObjects)
	{
		if (!object)
			continue;

		jObject::RemoveObject(object);
		delete object;
	}
	SceneBrowserLoadedObjects.clear();
}

void jGame::ClearSceneBrowserLoadedLights()
{
	for (jLight* light : SceneBrowserLoadedLights)
	{
		if (!light)
			continue;

#ifdef ENABLE_EDITOR_FEATURES
		if (g_Editor)
			g_Editor->Placement.UnregisterLight(light);
#endif

		jLight::RemoveLights(light);
		delete light;
	}
	SceneBrowserLoadedLights.clear();
}

int32 jGame::FindPathTracingSceneIndexById(const std::string& InSceneId) const
{
	if (InSceneId.empty())
		return -1;

	for (int32 i = 0; i < (int32)PathTracingSceneBrowser.Scenes.size(); ++i)
	{
		if (PathTracingSceneBrowser.Scenes[i].SceneId == InSceneId)
			return i;
	}

	return -1;
}

void jGame::ApplyScenePlacementPreset(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= (int32)PathTracingSceneBrowser.Scenes.size())
		return;

	const jScenePlacementPreset preset = GetScenePlacementPreset(PathTracingSceneBrowser.Scenes[InIndex]);
	if (!preset.IsValid)
		return;

	if (preset.Camera.IsValid && MainCamera)
	{
		const float distance = (preset.Camera.Target - preset.Camera.Position).Length();
		const Vector cameraUpPoint = preset.Camera.Position + preset.Camera.Up;
		jCamera::SetCamera(MainCamera
			, preset.Camera.Position
			, preset.Camera.Target
			, cameraUpPoint
			, preset.Camera.FovRad
			, preset.Camera.Near
			, preset.Camera.Far
			, (float)MainCamera->Width
			, (float)MainCamera->Height
			, MainCamera->IsPerspectiveProjection
			, (distance > 0.0f) ? distance : MainCamera->Distance);
		MainCamera->UpdateCamera();
		gOptions.CameraPos = MainCamera->Pos;
	}

	for (jLight* light : jLight::GetLights())
	{
		if (!light)
			continue;

		switch (light->GetLightType())
		{
		case ELightType::DIRECTIONAL:
		{
			if (!preset.DirectionalLight.IsValid)
				break;

			auto* directionalLight = static_cast<jDirectionalLight*>(light);
			directionalLight->SetDirection(preset.DirectionalLight.Direction);
			directionalLight->SetColor(preset.DirectionalLight.Color);
			break;
		}
		case ELightType::POINT:
		{
			if (!preset.PointLight.IsValid)
				break;

			auto* pointLight = static_cast<jPointLight*>(light);
			pointLight->SetPosition(preset.PointLight.Position);
			pointLight->SetColor(preset.PointLight.Color);
			pointLight->SetMaxDistance(preset.PointLight.MaxDistance);
			break;
		}
		case ELightType::SPOT:
		{
			if (!preset.SpotLight.IsValid)
				break;

			auto* spotLight = static_cast<jSpotLight*>(light);
			spotLight->SetPosition(preset.SpotLight.Position);
			spotLight->SetDirection(preset.SpotLight.Direction);
			spotLight->SetColor(preset.SpotLight.Color);
			spotLight->SetMaxDistance(preset.SpotLight.MaxDistance);
			spotLight->SetConeAngles(preset.SpotLight.PenumbraRadian, preset.SpotLight.UmbraRadian);
			break;
		}
		default:
			break;
		}
	}

	std::vector<jLight*> assignedLights;
	assignedLights.reserve(preset.Lights.size());
	for (const auto& lightPlacement : preset.Lights)
	{
		if (!lightPlacement.IsValid)
			continue;

		jLight* selectedLight = nullptr;
		for (jLight* light : jLight::GetLights())
		{
			if (!light || light->GetLightType() != lightPlacement.Type)
				continue;
			if (std::find(assignedLights.begin(), assignedLights.end(), light) != assignedLights.end())
				continue;

			selectedLight = light;
			break;
		}

		if (!selectedLight)
		{
			switch (lightPlacement.Type)
			{
			case ELightType::DIRECTIONAL:
				selectedLight = jLight::CreateDirectionalLight(lightPlacement.Direction, lightPlacement.Color, Vector(1.0f), Vector(1.0f), 64.0f);
				break;
			case ELightType::POINT:
				selectedLight = jLight::CreatePointLight(lightPlacement.Position, lightPlacement.Color, lightPlacement.MaxDistance, Vector(1.0f), Vector(1.0f), 64.0f);
				break;
			case ELightType::SPOT:
				selectedLight = jLight::CreateSpotLight(lightPlacement.Position, lightPlacement.Direction, lightPlacement.Color
					, lightPlacement.MaxDistance, lightPlacement.PenumbraRadian, lightPlacement.UmbraRadian, Vector(1.0f), Vector(1.0f), 64.0f);
				break;
			default:
				break;
			}

			if (selectedLight)
			{
				jLight::AddLights(selectedLight);
				SceneBrowserLoadedLights.push_back(selectedLight);
#ifdef ENABLE_EDITOR_FEATURES
				if (g_Editor)
					g_Editor->Placement.RegisterLight(selectedLight);
#endif
			}
		}

		if (!selectedLight)
			continue;

		assignedLights.push_back(selectedLight);

		if (selectedLight->SupportsPosition())
			selectedLight->SetPosition(lightPlacement.Position);
		if (selectedLight->SupportsDirection())
			selectedLight->SetDirection(lightPlacement.Direction);

		switch (lightPlacement.Type)
		{
		case ELightType::DIRECTIONAL:
		{
			auto* directionalLight = static_cast<jDirectionalLight*>(selectedLight);
			directionalLight->SetColor(lightPlacement.Color);
			break;
		}
		case ELightType::POINT:
		{
			auto* pointLight = static_cast<jPointLight*>(selectedLight);
			pointLight->SetColor(lightPlacement.Color);
			pointLight->SetMaxDistance(lightPlacement.MaxDistance);
			break;
		}
		case ELightType::SPOT:
		{
			auto* spotLight = static_cast<jSpotLight*>(selectedLight);
			spotLight->SetColor(lightPlacement.Color);
			spotLight->SetMaxDistance(lightPlacement.MaxDistance);
			spotLight->SetConeAngles(lightPlacement.PenumbraRadian, lightPlacement.UmbraRadian);
			break;
		}
		default:
			break;
		}

		if (!lightPlacement.LogicName.empty())
		{
			const std::string logicName = lightPlacement.LogicName;
			const nlohmann::json logicParams = lightPlacement.LogicParams;
			selectedLight->PreUpdateLambda = [logicName, logicParams](jLight* InLight, float InDeltaTime)
			{
				jLightLogicContext context;
				context.DeltaTime = InDeltaTime;
				context.Params = &logicParams;
				jLightLogicRegistry::Get().Execute(logicName, InLight, context);
			};
		}
		else
		{
			selectedLight->PreUpdateLambda = nullptr;
		}
	}

	for (const auto& objectPlacement : preset.Objects)
	{
		if (!objectPlacement.IsValid)
			continue;

		jObject* spawnedObject = nullptr;
		switch (objectPlacement.Type)
		{
		case jScenePlacementPreset::EObjectType::Sphere:
			spawnedObject = jPrimitiveUtil::CreateSphere(objectPlacement.Position, objectPlacement.Radius
				, (uint32)objectPlacement.Slices, (uint32)objectPlacement.Stacks, objectPlacement.Scale, objectPlacement.Color);
			break;
		case jScenePlacementPreset::EObjectType::Quad:
			spawnedObject = jPrimitiveUtil::CreateQuad(objectPlacement.Position, objectPlacement.Size, objectPlacement.Scale, objectPlacement.Color);
			break;
		case jScenePlacementPreset::EObjectType::Triangle:
			spawnedObject = jPrimitiveUtil::CreateTriangle(objectPlacement.Position, objectPlacement.Size, objectPlacement.Scale, objectPlacement.Color);
			break;
		case jScenePlacementPreset::EObjectType::Cube:
			spawnedObject = jPrimitiveUtil::CreateCube(objectPlacement.Position, objectPlacement.Size, objectPlacement.Scale, objectPlacement.Color);
			break;
		case jScenePlacementPreset::EObjectType::Capsule:
			spawnedObject = jPrimitiveUtil::CreateCapsule(objectPlacement.Position, objectPlacement.Height, objectPlacement.Radius
				, objectPlacement.Slices, objectPlacement.Scale, objectPlacement.Color);
			break;
		case jScenePlacementPreset::EObjectType::Cone:
			spawnedObject = jPrimitiveUtil::CreateCone(objectPlacement.Position, objectPlacement.Height, objectPlacement.Radius
				, objectPlacement.Slices, objectPlacement.Scale, objectPlacement.Color);
			break;
		case jScenePlacementPreset::EObjectType::Cylinder:
			spawnedObject = jPrimitiveUtil::CreateCylinder(objectPlacement.Position, objectPlacement.Height, objectPlacement.Radius
				, objectPlacement.Slices, objectPlacement.Scale, objectPlacement.Color);
			break;
		default:
			break;
		}

		if (!spawnedObject)
			continue;

		jObject::AddObject(spawnedObject);
		SceneBrowserLoadedObjects.push_back(spawnedObject);
	}
}

void jGame::LoadPathTracingSceneByIndex(int32 InIndex, bool InRebuildRaytracingScene)
{
	if (InIndex < 0 || InIndex >= (int32)PathTracingSceneBrowser.Scenes.size())
		return;

	const jLoadableSceneDesc& sceneDesc = PathTracingSceneBrowser.Scenes[InIndex];
	PathTracingSceneBrowser.PendingLoadIndex = -1;

	if (gPathTracingScene)
	{
		g_rhi->Flush();
		gPathTracingScene->ClearSceneFor_jEngine(this);
		delete gPathTracingScene;
		gPathTracingScene = nullptr;
	}
	ClearSceneBrowserLoadedLights();
	ClearSceneBrowserLoadedObjects();

	const ESceneLoader resolvedLoader = ResolveSceneLoader(sceneDesc);
	const bool usePathTracingSceneLoader = (resolvedLoader == ESceneLoader::PathTracing);
	if (usePathTracingSceneLoader)
	{
		gPathTracingScene = jPathTracingLoadData::LoadPathTracingData(sceneDesc.FilePath);
		check(gPathTracingScene);
		if (!gPathTracingScene)
			return;
		gPathTracingScene->CreateSceneFor_jEngine(this);
	}
	else
	{
		const std::filesystem::path scenePath(sceneDesc.FilePath);
		const std::string materialRootDir = scenePath.has_parent_path() ? scenePath.parent_path().generic_string() : std::string();
		jMeshObject* loadedScene = jModelLoader::GetInstance().LoadFromFile(sceneDesc.FilePath.c_str()
			, materialRootDir.empty() ? nullptr : materialRootDir.c_str());
		check(loadedScene);
		if (!loadedScene)
			return;

		jObject::AddObject(loadedScene);
		SceneBrowserLoadedObjects.push_back(loadedScene);
	}

	PathTracingSceneBrowser.ActiveIndex = InIndex;
	PathTracingSceneBrowser.SelectedIndex = InIndex;
	PathTracingSceneBrowser.ActiveRenderPipeline = sceneDesc.RenderPipeline;
	PathTracingSceneBrowser.ActiveLoader = resolvedLoader;
	PathTracingSceneBrowser.LastLoadedSceneId = sceneDesc.SceneId;
	PathTracingSceneBrowser.LastLoadedLoader = resolvedLoader;
	SavePathTracingSceneBrowserSettings();

	switch (PathTracingSceneBrowser.ActiveRenderPipeline)
	{
	case ESceneRenderPipeline::Forward:
		gOptions.UseDeferredRenderer = false;
		break;
	case ESceneRenderPipeline::Deferred:
		gOptions.UseDeferredRenderer = true;
		break;
	case ESceneRenderPipeline::PathTracing:
	default:
		break;
	}

	ApplyScenePlacementPreset(InIndex);

	if (InRebuildRaytracingScene && GSupportRaytracing && g_rhi && g_rhi->RaytracingScene)
	{
		g_rhi->RaytracingScene->Clear();

		jRatracingInitializer Initializer;
		Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
		Initializer.RenderObjects = jObject::GetStaticRenderObject();
		g_rhi->RaytracingScene->CreateOrUpdateBLAS(Initializer);
		g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
		g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here

		Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
		g_rhi->RaytracingScene->CreateOrUpdateTLAS(Initializer);
		g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
		g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here
	}
}

void jGame::ProcessInput(float deltaTime)
{
	// If console is visible, don't process game input
	// All console keys (ESC, `, ') are handled by ImGui in RenderInputField
	if (jConsole::Get().IsVisible())
	{
		return;
	}

	// Note: ` and ' keys are handled in ImGui (jConsole.cpp RenderInputField)
	// to avoid double-triggering issues between Win32 and ImGui input systems

	static float MoveDistancePerSecond = 200.0f;
	//static float MoveDistancePerSecond = 1.0f;
	//static float MoveDistancePerSecond = 10.0f;
	const float CurrentDistance = MoveDistancePerSecond * deltaTime;

	// Process Key Event
	if (IsKeyDown(EInputKey::A)) MainCamera->MoveShift(-CurrentDistance);
	if (IsKeyDown(EInputKey::D)) MainCamera->MoveShift(CurrentDistance);
	//if (g_KeyState['1']) MainCamera->RotateForwardAxis(-0.1f);
	//if (g_KeyState['2']) MainCamera->RotateForwardAxis(0.1f);
	//if (g_KeyState['3']) MainCamera->RotateUpAxis(-0.1f);
	//if (g_KeyState['4']) MainCamera->RotateUpAxis(0.1f);
	//if (g_KeyState['5']) MainCamera->RotateRightAxis(-0.1f);
	//if (g_KeyState['6']) MainCamera->RotateRightAxis(0.1f);
	if (IsKeyDown(EInputKey::W)) MainCamera->MoveForward(CurrentDistance);
	if (IsKeyDown(EInputKey::S)) MainCamera->MoveForward(-CurrentDistance);
	if (IsKeyDown(EInputKey::PLUS)) MoveDistancePerSecond = Max(MoveDistancePerSecond + 10.0f, 0.0f);
	if (IsKeyDown(EInputKey::MINUS)) MoveDistancePerSecond = Max(MoveDistancePerSecond - 10.0f, 0.0f);

#ifdef ENABLE_EDITOR_FEATURES
	// Editor-specific input handling (e.g., Placement Tool)
	if (g_Editor)
	{
		g_Editor->Placement.ProcessInput(deltaTime, MainCamera, gOptions.LightColorScale);
	}
#endif
}

void jGame::Setup()
{
 	srand(static_cast<uint32>(time(NULL)));

	// Register test console variables
	{
		// External variable examples (connected to gOptions)
		static jConsoleVariableBool* cvar_UseVRS = new jConsoleVariableBool("r.vrs", &gOptions.UseVRS, "Enable Variable Rate Shading");
		static jConsoleVariableBool* cvar_UseSSGI = new jConsoleVariableBool("r.ssgi.enable", &gOptions.UseSSGI, "Enable Screen Space Global Illumination");
        static jConsoleVariableBool* cvar_UseHWRTDirectLighting = new jConsoleVariableBool("r.hwrt.direct_lighting.enable", &gOptions.UseHWRTDirectLighting, "Enable HWRT direct lighting path");
        static jConsoleVariableInt* cvar_HWRTDirectLightingMode = new jConsoleVariableInt("r.hwrt.direct_lighting.mode", &gOptions.HWRTDirectLightingMode, "HWRT direct lighting mode (0=DispatchRays, 1=InlineRayQuery)");
        static jConsoleVariableBool* cvar_UseSurfelGI = new jConsoleVariableBool("r.surfelgi.enable", &gOptions.UseSurfelGI, "Enable SurfelGI prototype pool update");
        static jConsoleVariableInt* cvar_SurfelGIReservoirPerCellLimit = new jConsoleVariableInt("r.surfelgi.reservoir.per_cell_limit", &gOptions.SurfelGIReservoirPerCellLimit, "SurfelGI reservoir per-cell page size limit");
        static jConsoleVariableFloat* cvar_SurfelGIFaceMarginRadiusScale = new jConsoleVariableFloat("r.surfelgi.face_margin_radius_scale", &gOptions.SurfelGIFaceMarginRadiusScale, "SurfelGI reservoir candidate face margin radius scale");
static jConsoleVariableBool* cvar_SurfelGIInlineRayEnable = new jConsoleVariableBool("r.surfelgi.inline_ray.enable", &gOptions.SurfelGIInlineRayEnable, "Enable SurfelGI inline ray irradiance gather pass");
static jConsoleVariableBool* cvar_SurfelGIInlineRayGuideEnable = new jConsoleVariableBool("r.surfelgi.inline_ray.guide_enable", &gOptions.SurfelGIInlineRayGuideEnable, "Enable SurfelGI inline ray directional guiding");
static jConsoleVariableInt* cvar_SurfelGIInlineRayCount = new jConsoleVariableInt("r.surfelgi.inline_ray.count", &gOptions.SurfelGIInlineRayCount, "SurfelGI inline ray count per surfel");
        static jConsoleVariableFloat* cvar_SurfelGIInlineRayMaxDistance = new jConsoleVariableFloat("r.surfelgi.inline_ray.max_distance", &gOptions.SurfelGIInlineRayMaxDistance, "SurfelGI inline ray max trace distance");
        static jConsoleVariableFloat* cvar_SurfelGIInlineRayNormalBias = new jConsoleVariableFloat("r.surfelgi.inline_ray.normal_bias", &gOptions.SurfelGIInlineRayNormalBias, "SurfelGI inline ray origin normal bias");
        static jConsoleVariableFloat* cvar_SurfelGIInlineRayHistoryBlend = new jConsoleVariableFloat("r.surfelgi.inline_ray.history_blend", &gOptions.SurfelGIInlineRayHistoryBlend, "SurfelGI inline ray MSME short-window control");
        static jConsoleVariableFloat* cvar_SurfelGIIntensity = new jConsoleVariableFloat("r.surfelgi.intensity", &gOptions.SurfelGIIntensity, "SurfelGI resolve/apply intensity");
        static jConsoleVariableInt* cvar_SurfelGIIrradianceDebugMode = new jConsoleVariableInt("r.surfelgi.visualize.irradiance_mode", &gOptions.SurfelGIIrradianceDebugMode, "SurfelGI irradiance visualize mode (0=mean, 1=short_mean, 2=variance, 3=inconsistency, 4=count_vbbr)");
        static jConsoleVariableBool* cvar_SurfelGIHoverRayDebug = new jConsoleVariableBool("r.surfelgi.visualize.hover_rays", &gOptions.ShowSurfelGIHoverRayDebug, "Show hover surfel inline-ray directions (red=guide, blue=cosine)");
        static jConsoleVariableInt* cvar_SurfelGISurfelsPerCell[SURFEL_GI_CASCADE_COUNT] = {};
        for (int32 cascade = 0; cascade < SURFEL_GI_CASCADE_COUNT; ++cascade)
        {
            char surfelsPerCellName[128];
            char surfelsPerCellDesc[128];
            sprintf_s(surfelsPerCellName, "r.surfelgi.cascade%d.surfels_per_cell", cascade);
            sprintf_s(surfelsPerCellDesc, "SurfelGI cascade %d desired surfels per cell", cascade);

            if (!cvar_SurfelGISurfelsPerCell[cascade])
            {
                cvar_SurfelGISurfelsPerCell[cascade] = new jConsoleVariableInt(surfelsPerCellName, &gOptions.SurfelGISurfelsPerCell[cascade], surfelsPerCellDesc);
            }
        }
		static jConsoleVariableInt* cvar_SSGIRayCount = new jConsoleVariableInt("r.ssgi.raycount", &gOptions.SSGIRayCount, "SSGI ray count per pixel");
		static jConsoleVariableFloat* cvar_SSGIIntensity = new jConsoleVariableFloat("r.ssgi.intensity", &gOptions.SSGIIntensity, "SSGI intensity multiplier");

		// Internal variable examples (standalone test variables)
		static jConsoleVariableBool* cvar_DebugDraw = new jConsoleVariableBool("debug.draw", false, "Enable debug drawing");
		static jConsoleVariableInt* cvar_DebugLevel = new jConsoleVariableInt("debug.level", 0, "Debug verbosity level (0-3)");
		static jConsoleVariableFloat* cvar_TimeScale = new jConsoleVariableFloat("game.timescale", 1.0f, "Game time scale multiplier");
		static jConsoleVariableString* cvar_PlayerName = new jConsoleVariableString("player.name", "Player", "Player name");

		jConsole::Get().Log("Test console variables registered.");
	}

	// Create main camera
    const Vector mainCameraPos(172.66f, 160.0f, -180.63f);
    const Vector mainCameraTarget(0.0f, 0.0f, 0.0f);
    MainCamera = jCamera::CreateCamera(mainCameraPos, mainCameraTarget, mainCameraPos + Vector(0.0, 1.0, 0.0), DegreeToRadian(45.0f), 10.0f, 1500.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT, true);
    jCamera::AddCamera(0, MainCamera);
	
	InitializePathTracingSceneBrowser();

	g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here
	if (GSupportRaytracing)
	{
		jRatracingInitializer Initializer;
		Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
		Initializer.RenderObjects = jObject::GetStaticRenderObject();
		g_rhi->RaytracingScene->CreateOrUpdateBLAS(Initializer);
		g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
		g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here

		Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
		g_rhi->RaytracingScene->CreateOrUpdateTLAS(Initializer);
		g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
		g_rhi->Finish(); // todo : Instead of this, it needs UAV barrier here
	}

	// todo : Need to move
	{
		if (!jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive)
			jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

		if (!jPointLightDrawCommandGenerator::PointLightSphere)
			jPointLightDrawCommandGenerator::PointLightSphere = jPrimitiveUtil::CreateSphere(Vector::ZeroVector, 1.0, 16, 8, Vector(1.0f), Vector4::OneVector);

		if (!jSpotLightDrawCommandGenerator::SpotLightCone)
			jSpotLightDrawCommandGenerator::SpotLightCone = jPrimitiveUtil::CreateCone(Vector::ZeroVector, 1.0, 1.0, 20, Vector::OneVector, Vector4::OneVector, false, false);
	}
}

void jGame::Update(float deltaTime)
{
	SCOPE_DEBUG_EVENT(g_rhi, "Game::Update");

	if (PathTracingSceneBrowser.PendingLoadIndex >= 0
		&& PathTracingSceneBrowser.PendingLoadIndex != PathTracingSceneBrowser.ActiveIndex)
	{
		LoadPathTracingSceneByIndex(PathTracingSceneBrowser.PendingLoadIndex, true);
	}

	// Update application property by using UI Pannel.
	// UpdateAppSetting();

	// Update main camera
	if (MainCamera)
	{
		MainCamera->UpdateCamera();

		gOptions.CameraPos = MainCamera->Pos;
	}
	//// Update lights
	//const int32 numOfLights = MainCamera->GetNumOfLight();
	//for (int32 i = 0; i < numOfLights; ++i)
	//{
	//	auto light = MainCamera->GetLight(i);
	//	JASSERT(light);
	//	light->Update(deltaTime);
	//}

	//for (auto iter : jObject::GetStaticObject())
	//	iter->Update(deltaTime);

	//for (auto& iter : jObject::GetBoundBoxObject())
	//	iter->Update(deltaTime);

	//for (auto& iter : jObject::GetBoundSphereObject())
	//	iter->Update(deltaTime);

	//for (auto& iter : jObject::GetDebugObject())
	//	iter->Update(deltaTime);

	// Update object which have dirty flag
	jObject::FlushDirtyState();

    // Sync raytracing scene with placement/editor changes
    if (GSupportRaytracing && g_rhi && g_rhi->RaytracingScene && g_rhi->RaytracingScene->ShouldUpdate())
    {
        jRatracingInitializer Initializer;
        Initializer.RenderObjects = jObject::GetStaticRenderObject();

        Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
        g_rhi->RaytracingScene->CreateOrUpdateBLAS(Initializer);
        g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
        g_rhi->Finish(); // todo : replace with proper UAV barrier

        Initializer.CommandBuffer = g_rhi->BeginSingleTimeCommands();
        g_rhi->RaytracingScene->CreateOrUpdateTLAS(Initializer);
        g_rhi->EndSingleTimeCommands(Initializer.CommandBuffer);
        g_rhi->Finish(); // todo : replace with proper UAV barrier
    }

	//// Render all objects by using selected renderer
	//Renderer->Render(MainCamera);

    for (auto& iter : jObject::GetStaticObject())
    {
        iter->Update(deltaTime);

		for(auto& RenderObject : iter->RenderObjects)
			RenderObject->UpdateWorldMatrix();
    }

	for (auto& iter : jObject::GetDebugObject())
	{
		iter->Update(deltaTime);

        for (auto& RenderObject : iter->RenderObjects)
            RenderObject->UpdateWorldMatrix();
	}

	for (auto light : jLight::GetLights())
	{
		light->Update(deltaTime);
	}
}

void jGame::Draw()
{
	SCOPE_CPU_PROFILE(Draw);
	SCOPE_DEBUG_EVENT(g_rhi, "Game::Draw");

	{
		std::shared_ptr<jRenderFrameContext> renderFrameContext = g_rhi->BeginRenderFrame();
		if (!renderFrameContext)
			return;
		
		jView View(MainCamera, jLight::GetLights());
		View.PrepareViewUniformBufferShaderBindingInstance();

#if USE_PATH_TRACING
		if (IsUsingPathTracingRenderer())
		{
			jRenderer_PathTracing renderer(renderFrameContext, View);
			renderer.Render();
		}
		else
		{
			jRenderer renderer(renderFrameContext, View);
			renderer.Render();
		}
#else
        jRenderer renderer(renderFrameContext, View);
        renderer.Render();
#endif // USE_PATH_TRACING

		g_rhi->EndRenderFrame(renderFrameContext);
	}
    jMemStack::Get()->Flush();
}

void jGame::OnMouseButton()
{
#ifdef ENABLE_EDITOR_FEATURES
	// Handle object picking on left mouse button click
	if (g_Editor && g_Editor->Placement.EnablePlacementMode && g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Clicked)
	{
		// Don't pick when manipulating Gizmo
		if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
			return;

		// Don't pick when clicking on UI
		if (ImGui::IsAnyItemHovered() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
			return;

		// Get current mouse position
		double xpos, ypos;
		if (IsUseVulkan())
		{
			GLFWwindow* window = static_cast<GLFWwindow*>(g_rhi->GetWindow());
			glfwGetCursorPos(window, &xpos, &ypos);
		}
		else if (IsUseDX12())
		{
			POINT pt;
			if (GetCursorPos(&pt))
			{
				HWND hwnd = (HWND)g_rhi->GetWindow();
				ScreenToClient(hwnd, &pt);
				xpos = pt.x;
				ypos = pt.y;
			}
		}

		// Request object picking
		g_Editor->Placement.bPickRequested = true;
		g_Editor->Placement.PickMouseX = (int32)xpos;
		g_Editor->Placement.PickMouseY = (int32)ypos;
	}
#endif
}

void jGame::OnMouseMove(int32 xOffset, int32 yOffset)
{
	if (g_MouseState[MouseButtonIndex(EMouseButtonType::LEFT)].Down)
	{
#ifdef ENABLE_EDITOR_FEATURES
		// Don't rotate camera when manipulating Gizmo
		if (ImGuizmo::IsUsing() || ImGuizmo::IsOver())
			return;
#endif

		constexpr float PitchScale = 0.0025f;
		constexpr float YawScale = 0.0025f;
		if (MainCamera)
			MainCamera->SetEulerAngle(MainCamera->GetEulerAngle() + Vector(yOffset * PitchScale, xOffset * YawScale, 0.0f));
	}
}

void jGame::Resize(int32 width, int32 height)
{
	if (MainCamera)
	{
		MainCamera->Width = width;
		MainCamera->Height = height;
	}
}

void jGame::Release()
{
	g_rhi->Flush();

	delete jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive;
	jDirectionalLightDrawCommandGenerator::GlobalFullscreenPrimitive = nullptr;
	
	delete jPointLightDrawCommandGenerator::PointLightSphere;
	jPointLightDrawCommandGenerator::PointLightSphere = nullptr;
	
	delete jSpotLightDrawCommandGenerator::SpotLightCone;
	jSpotLightDrawCommandGenerator::SpotLightCone = nullptr;

	delete jSceneRenderTarget::GlobalFullscreenPrimitive;
	jSceneRenderTarget::GlobalFullscreenPrimitive = nullptr;

	for(auto it : jObject::s_StaticObjects)
	{
		delete it;
	}
	for(auto it : jLight::s_Lights)
	{
		delete it;
	}

	delete gPathTracingScene;
	gPathTracingScene = nullptr;
	
	delete MainCamera;
    MainCamera = nullptr;

    ReleaseSurfelGIResources();
    jSceneRenderTarget::ReleasePersistentResources();
}

void jGame::SpawnGraphTestFunc()
{
	Vector PerspectiveVector[90];
	Vector OrthographicVector[90];
	{
		{
			static jCamera* pCamera = jCamera::CreateCamera(Vector(0.0), Vector(0.0, 0.0, 1.0), Vector(0.0, 1.0, 0.0), DegreeToRadian(90), 10.0, 100.0, 100.0, 100.0, true);
			pCamera->UpdateCamera();
			int cnt = 0;
			auto MV = pCamera->Projection * pCamera->View;
			for (int i = 0; i < 90; ++i)
			{
				PerspectiveVector[cnt++] = MV.TransformPoint(Vector({ 0.0f, 0.0f, 10.0f + static_cast<float>(i) }));
			}

			for (int i = 0; i < _countof(PerspectiveVector); ++i)
				PerspectiveVector[i].z = (PerspectiveVector[i].z + 1.0f) * 0.5f;
		}
		{
			static jCamera* pCamera = jCamera::CreateCamera(Vector(0.0), Vector(0.0, 0.0, 1.0), Vector(0.0, 1.0, 0.0), DegreeToRadian(90), 10.0, 100.0, 100.0, 100.0, false);
			pCamera->UpdateCamera();
			int cnt = 0;
			auto MV = pCamera->Projection * pCamera->View;
			for (int i = 0; i < 90; ++i)
			{
				OrthographicVector[cnt++] = MV.TransformPoint(Vector({ 0.0f, 0.0f, 10.0f + static_cast<float>(i) }));
			}

			for (int i = 0; i < _countof(OrthographicVector); ++i)
				OrthographicVector[i].z = (OrthographicVector[i].z + 1.0f) * 0.5f;
		}
	}
	std::vector<Vector2> graph1;
	std::vector<Vector2> graph2;

	float scale = 100.0f;
	for (int i = 0; i < _countof(PerspectiveVector); ++i)
		graph1.push_back(Vector2(static_cast<float>(i*2), PerspectiveVector[i].z * scale));
	for (int i = 0; i < _countof(OrthographicVector); ++i)
		graph2.push_back(Vector2(static_cast<float>(i*2), OrthographicVector[i].z* scale));

	auto graphObj1 = jPrimitiveUtil::CreateGraph2D({ 360, 350 }, {360, 300}, graph1);
	jObject::AddUIDebugObject(graphObj1);

	auto graphObj2 = jPrimitiveUtil::CreateGraph2D({ 360, 700 }, { 360, 300 }, graph2);
	jObject::AddUIDebugObject(graphObj2);
}




