#include "pch.h"

#ifdef ENABLE_EDITOR_FEATURES

#include "jPlacementTool.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jLight.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "jPrimitiveUtil.h"
#include "jOptions.h"
#include "ImGui/jImGui.h"

//////////////////////////////////////////////////////////////////////////
// Transform Validation Helpers
//////////////////////////////////////////////////////////////////////////

namespace
{
	// Validate if a float value is finite and not NaN
	inline bool IsValidFloat(float value)
	{
		return !isnan(value) && !isinf(value) && isfinite(value);
	}

	// Validate if a scale value is valid (positive and above minimum threshold)
	inline bool IsValidScale(float value)
	{
		constexpr float MIN_SCALE = 0.001f;
		return IsValidFloat(value) && value >= MIN_SCALE;
	}

	// Validate if a Vector contains valid float values
	inline bool IsValidVector(const Vector& vec)
	{
		return IsValidFloat(vec.x) && IsValidFloat(vec.y) && IsValidFloat(vec.z);
	}

	// Validate if a Vector is suitable for scale (all components positive)
	inline bool IsValidScaleVector(const Vector& vec)
	{
		return IsValidScale(vec.x) && IsValidScale(vec.y) && IsValidScale(vec.z);
	}

	// Clamp scale to minimum valid value
	inline Vector ClampScale(const Vector& scale)
	{
		constexpr float MIN_SCALE = 0.001f;
		return Vector(
			Max(scale.x, MIN_SCALE),
			Max(scale.y, MIN_SCALE),
			Max(scale.z, MIN_SCALE)
		);
	}

	// Check if an index is in the selection list
	inline bool IsIndexSelected(const std::vector<int32>& indices, int32 index)
	{
		return std::find(indices.begin(), indices.end(), index) != indices.end();
	}

	// Toggle index in selection list (add if not present, remove if present)
	inline void ToggleIndexInSelection(std::vector<int32>& indices, int32 index)
	{
		auto it = std::find(indices.begin(), indices.end(), index);
		if (it != indices.end())
			indices.erase(it);  // Remove if present
		else
			indices.push_back(index);  // Add if not present
	}

	// Add index to selection if not already present
	inline void AddIndexToSelection(std::vector<int32>& indices, int32 index)
	{
		if (!IsIndexSelected(indices, index))
			indices.push_back(index);
	}

	inline EPlacementLightType ToPlacementLightType(ELightType lightType)
	{
		switch (lightType)
		{
		case ELightType::POINT:
			return EPlacementLightType::POINT;
		case ELightType::SPOT:
			return EPlacementLightType::SPOT;
		case ELightType::DIRECTIONAL:
			return EPlacementLightType::DIRECTIONAL;
		default:
			return EPlacementLightType::NONE;
		}
	}
}

//////////////////////////////////////////////////////////////////////////

jPlacementTool::jPlacementTool()
{
}

jPlacementTool::~jPlacementTool()
{
}

bool jPlacementTool::IsKeyPressed(EInputKey key) const
{
	return PressedKeys[InputKeyIndex(key)];
}

void jPlacementTool::ProcessInput(float deltaTime, jCamera* mainCamera, float lightColorScale)
{
	// Consume edges even while a field/modal/another panel owns input. A held
	// Delete or P must not fire when focus returns to the placement viewport.
	for (size_t i = 0; i < PreviousKeys.size(); ++i)
	{
		const bool down = IsKeyDown(static_cast<EInputKey>(i));
		PressedKeys[i] = down && !PreviousKeys[i];
		PreviousKeys[i] = down;
	}
	const auto& io = ImGui::GetIO();
	if (!EnablePlacementMode || io.WantTextInput || ImGui::IsAnyItemActive()
		|| (io.WantCaptureKeyboard && !SceneListHasFocus) || ImGuizmo::IsUsing())
		return;

	if (IsKeyPressed(EInputKey::G) && IsKeyDown(EInputKey::CTRL))
		UseSnap = !UseSnap;
	if (IsKeyDown(EInputKey::CTRL) || IsKeyDown(EInputKey::ALT))
		return;

	ProcessGizmoHotkeys();
	ProcessTypeSelectionHotkeys();
	ProcessPlacementHotkeys(mainCamera, lightColorScale);
	if (IsKeyPressed(EInputKey::UP))
		MoveSceneSelection(-1);
	if (IsKeyPressed(EInputKey::DOWN))
		MoveSceneSelection(1);
	if (IsKeyPressed(EInputKey::ESCAPE))
		SelectPlacedObject(-1);
	if (IsKeyPressed(EInputKey::DELETE_KEY))
		DeletePlacedObject(SelectedPlacedObjectIndex);
}

void jPlacementTool::ProcessGizmoHotkeys()
{
	if (IsKeyDown(EInputKey::SHIFT))
	{
		if (IsKeyPressed(EInputKey::W)) GizmoOperation = ImGuizmo::TRANSLATE;
		if (IsKeyPressed(EInputKey::E)) GizmoOperation = ImGuizmo::ROTATE;
		if (IsKeyPressed(EInputKey::R)) GizmoOperation = ImGuizmo::SCALE;
	}
	if (IsKeyPressed(EInputKey::Q))
		GizmoMode = GizmoMode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
}

void jPlacementTool::ProcessTypeSelectionHotkeys()
{
	if (IsKeyPressed(EInputKey::L) || IsKeyPressed(EInputKey::K) || IsKeyPressed(EInputKey::J))
	{
		CurrentTab = EPlacementTab::LIGHT;
		SelectedLightType = IsKeyPressed(EInputKey::L) ? EPlacementLightType::POINT
			: IsKeyPressed(EInputKey::K) ? EPlacementLightType::SPOT : EPlacementLightType::DIRECTIONAL;
	}
	if (IsKeyPressed(EInputKey::N) || IsKeyPressed(EInputKey::M))
	{
		CurrentTab = EPlacementTab::SHAPE;
		SelectedShapeType = IsKeyPressed(EInputKey::N) ? EPlacementShapeType::CUBE : EPlacementShapeType::SPHERE;
	}
}

void jPlacementTool::ProcessPlacementHotkeys(jCamera* mainCamera, float lightColorScale)
{
	if (IsKeyPressed(EInputKey::P))
		PlaceSelectedObject(mainCamera, lightColorScale);
}

void jPlacementTool::PlaceSelectedObject(jCamera* mainCamera, float lightColorScale)
{
	if (!mainCamera)
		return;

	const size_t previousCount = PlacedObjects.size();
	if (CurrentTab == EPlacementTab::LIGHT)
	{
		PlaceLight(SelectedLightType, mainCamera, lightColorScale);
	}
	else if (CurrentTab == EPlacementTab::SHAPE)
	{
		PlaceShape(SelectedShapeType, mainCamera);
	}
	if (PlacedObjects.size() > previousCount)
	{
		SceneListDirty = true;
		SetSceneFilter("");
		SelectPlacedObject(static_cast<int32>(PlacedObjects.size() - 1));
	}
}

void jPlacementTool::PlaceLight(EPlacementLightType lightType, jCamera* mainCamera, float lightColorScale)
{
	Vector cameraPos = mainCamera->Pos;
	Vector cameraDir = mainCamera->GetForwardVector();

	// Place 100 units in front of camera so it's visible
	Vector placementPos = cameraPos + cameraDir * 100.0f;

	switch (lightType)
	{
	case EPlacementLightType::POINT:
		CreatePointLight(placementPos, mainCamera, lightColorScale);
		break;
	case EPlacementLightType::SPOT:
		CreateSpotLight(placementPos, cameraDir, mainCamera, lightColorScale);
		break;
	case EPlacementLightType::DIRECTIONAL:
		CreateDirectionalLight(cameraDir, placementPos, mainCamera, lightColorScale);
		break;
	case EPlacementLightType::NONE:
	default:
		break;
	}
}

void jPlacementTool::PlaceShape(EPlacementShapeType shapeType, jCamera* mainCamera)
{
	Vector cameraPos = mainCamera->Pos;
	Vector cameraDir = mainCamera->GetForwardVector();

	// Place 100 units in front of camera so it's visible
	Vector placementPos = cameraPos + cameraDir * 100.0f;

	switch (shapeType)
	{
	case EPlacementShapeType::CUBE:
		CreateCube(placementPos);
		break;
	case EPlacementShapeType::SPHERE:
		CreateSphere(placementPos);
		break;
	case EPlacementShapeType::NONE:
	default:
		break;
	}
}

void jPlacementTool::CreatePointLight(const Vector& position, jCamera* camera, float lightColorScale)
{
	constexpr float DebugIconScale = 10.0f;
	Vector lightColor = Vector(1.0f, 1.0f, 1.0f);  // White

	auto* newLight = jLight::CreatePointLight(
		position,
		lightColor * lightColorScale,
		1500.0f,  // maxDistance
		Vector(1.0f, 1.0f, 1.0f),  // diffuseIntensity
		Vector(1.0f, 1.0f, 1.0f),  // specularIntensity
		64.0f  // specularPower
	);

	jLight::AddLights(newLight);

	// Create debug visualization
	auto* debugObj = jPrimitiveUtil::CreatePointLightDebug(
		Vector(DebugIconScale), camera, static_cast<jPointLight*>(newLight), "Image/bulb.png");
	jObject::AddDebugObject(debugObj->BillboardObject);

	// Add to placed objects
	PlacedObjectInfo info;
	info.Object = debugObj->BillboardObject;
	info.Type = EPlacedObjectType::LIGHT;
	info.LightType = EPlacementLightType::POINT;
	info.LightPtr = newLight;
	PlacedObjects.push_back(info);
}

void jPlacementTool::CreateSpotLight(const Vector& position, const Vector& direction, jCamera* camera, float lightColorScale)
{
	constexpr float DebugIconScale = 10.0f;
	Vector lightColor = Vector(1.0f, 1.0f, 0.0f);  // Yellow

	auto* newLight = jLight::CreateSpotLight(
		position,
		direction,
		lightColor * lightColorScale,
		2000.0f,
		0.35f,
		1.0f,
		Vector(1.0f, 1.0f, 1.0f),
		Vector(1.0f),
		64.0f
	);

	jLight::AddLights(newLight);

	// Create debug visualization
	auto* debugObj = jPrimitiveUtil::CreateSpotLightDebug(
		Vector(DebugIconScale), camera, static_cast<jSpotLight*>(newLight), "Image/spot.png");
	jObject::AddDebugObject(debugObj->BillboardObject);

	// Add to placed objects
	PlacedObjectInfo info;
	info.Object = debugObj->BillboardObject;
	info.Type = EPlacedObjectType::LIGHT;
	info.LightType = EPlacementLightType::SPOT;
	info.LightPtr = newLight;
	PlacedObjects.push_back(info);
}

void jPlacementTool::CreateDirectionalLight(const Vector& direction, const Vector& debugPosition, jCamera* camera, float lightColorScale)
{
	constexpr float DebugIconScale = 10.0f;
	Vector lightColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;

	auto* newLight = jLight::CreateDirectionalLight(
		direction,
		lightColor,
		Vector(1.0f, 1.0f, 1.0f),  // diffuseIntensity
		Vector(1.0f, 1.0f, 1.0f),  // specularIntensity
		64.0f  // specularPower
	);

	jLight::AddLights(newLight);

	// Create debug visualization (at provided position for visibility)
	float length = 50.0f;
	auto* debugObj = jPrimitiveUtil::CreateDirectionalLightDebug(
		debugPosition, Vector(DebugIconScale), length, camera,
		static_cast<jDirectionalLight*>(newLight), "Image/sun.png");
	jObject::AddDebugObject(debugObj->BillboardObject);

	// Add to placed objects
	PlacedObjectInfo info;
	info.Object = debugObj->BillboardObject;
	info.Type = EPlacedObjectType::LIGHT;
	info.LightType = EPlacementLightType::DIRECTIONAL;
	info.LightPtr = newLight;
	PlacedObjects.push_back(info);
}

void jPlacementTool::CreateCube(const Vector& position)
{
	Vector size = Vector(20.0f, 20.0f, 20.0f);
	Vector scale = Vector::OneVector;
	Vector4 color = Vector4(0.7f, 0.7f, 0.7f, 1.0f);

	auto* cube = jPrimitiveUtil::CreateCube(position, size, scale, color);

	// AddObject also registers with the editor. Publish the metadata first so
	// that callback finds this entry instead of adding a second one.
	PlacedObjectInfo info;
	info.Object = cube;
	info.Type = EPlacedObjectType::SHAPE;
	info.ShapeType = EPlacementShapeType::CUBE;
	PlacedObjects.push_back(info);
	jObject::AddObject(cube);
}

void jPlacementTool::CreateSphere(const Vector& position)
{
	float radius = 15.0f;
	uint32 slices = 32;
	uint32 stacks = 16;
	Vector scale = Vector::OneVector;
	Vector4 color = Vector4(0.7f, 0.7f, 0.7f, 1.0f);

	auto* sphere = jPrimitiveUtil::CreateSphere(position, radius, slices, stacks, scale, color, false, true);

	// AddObject's registration callback must reuse this entry.
	PlacedObjectInfo info;
	info.Object = sphere;
	info.Type = EPlacedObjectType::SHAPE;
	info.ShapeType = EPlacementShapeType::SPHERE;
	PlacedObjects.push_back(info);
	jObject::AddObject(sphere);
}

void jPlacementTool::DeletePlacedObject(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(PlacedObjects.size()))
		return;

	// Scene removal calls back into UnregisterStaticObject. Remove our entry
	// first and keep a value copy so that callback cannot invalidate a reference
	// or make us erase the next object (or end()) a second time.
	const auto objectInfo = PlacedObjects[index];
	PlacedObjects.erase(PlacedObjects.begin() + index);
	SceneListDirty = true;
	SelectObject(static_cast<jObject*>(nullptr));

	// Remove from scene based on type
	if (objectInfo.Type == EPlacedObjectType::LIGHT)
	{
		// Remove light
		if (objectInfo.LightPtr)
		{
			jLight::RemoveLights(objectInfo.LightPtr);
		}
		// Remove debug object
		if (objectInfo.Object)
		{
			jObject::RemoveDebugObject(objectInfo.Object);
		}
	}
	else if (objectInfo.Type == EPlacedObjectType::SHAPE)
	{
		// Remove shape
		if (objectInfo.Object)
		{
			jObject::RemoveObject(objectInfo.Object);
		}
	}

}

void jPlacementTool::RenderUI(jCamera* mainCamera)
{
	ImGuizmo::BeginFrame();
	ImGui::PushID(this);
	RenderTabBar();
	ImGui::BeginDisabled(!mainCamera);
	if (CurrentTab == EPlacementTab::LIGHT)
		RenderLightTab(mainCamera, gOptions.LightColorScale);
	else
		RenderShapeTab(mainCamera);
	ImGui::SameLine();
	if (ImGui::Button("Add (P)", ImVec2(96, 0)))
		PlaceSelectedObject(mainCamera, gOptions.LightColorScale);
	ImGui::EndDisabled();
	ImGui::TextDisabled(mainCamera ? "Adds 100 units in front of the camera."
		: "A scene camera is required to add objects.");
	ImGui::Separator();
	RenderPlacedObjectsList(mainCamera);
	ImGui::PopID();
}

void jPlacementTool::RenderTabBar()
{
	ImGui::TextUnformatted("Add");
	ImGui::SameLine();
	if (ImGui::RadioButton("Light", CurrentTab == EPlacementTab::LIGHT))
		CurrentTab = EPlacementTab::LIGHT;
	ImGui::SameLine();
	if (ImGui::RadioButton("Shape", CurrentTab == EPlacementTab::SHAPE))
	{
		CurrentTab = EPlacementTab::SHAPE;
		if (SelectedShapeType == EPlacementShapeType::NONE)
			SelectedShapeType = EPlacementShapeType::CUBE;
	}
}

void jPlacementTool::RenderLightTab(jCamera* mainCamera, float lightColorScale)
{
	const char* names[] = { "Point Light (L)", "Spot Light (K)", "Directional Light (J)" };
	int type = SelectedLightType == EPlacementLightType::SPOT ? 1
		: SelectedLightType == EPlacementLightType::DIRECTIONAL ? 2 : 0;
	ImGui::SetNextItemWidth(Max(80.0f, ImGui::GetContentRegionAvail().x - 104.0f));
	if (ImGui::Combo("##LightType", &type, names, 3))
		SelectedLightType = type == 1 ? EPlacementLightType::SPOT
			: type == 2 ? EPlacementLightType::DIRECTIONAL : EPlacementLightType::POINT;
}

void jPlacementTool::RenderShapeTab(jCamera* mainCamera)
{
	const char* names[] = { "Cube (N)", "Sphere (M)" };
	int type = SelectedShapeType == EPlacementShapeType::SPHERE ? 1 : 0;
	ImGui::SetNextItemWidth(Max(80.0f, ImGui::GetContentRegionAvail().x - 104.0f));
	if (ImGui::Combo("##ShapeType", &type, names, 2))
		SelectedShapeType = type == 1 ? EPlacementShapeType::SPHERE : EPlacementShapeType::CUBE;
}

void jPlacementTool::RenderGizmoControls()
{
	const char* operations[] = { "Move (Shift+W)", "Rotate (Shift+E)", "Scale (Shift+R)" };
	int operation = GizmoOperation == ImGuizmo::ROTATE ? 1 : GizmoOperation == ImGuizmo::SCALE ? 2 : 0;
	ImGui::SetNextItemWidth(145);
	if (ImGui::Combo("##Operation", &operation, operations, 3))
		GizmoOperation = operation == 1 ? ImGuizmo::ROTATE : operation == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	const char* spaces[] = { "World (Q)", "Local (Q)" };
	int space = GizmoMode == ImGuizmo::LOCAL ? 1 : 0;
	ImGui::SetNextItemWidth(100);
	if (ImGui::Combo("##Space", &space, spaces, 2))
		GizmoMode = space == 1 ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
	ImGui::Checkbox("Snap (Ctrl+G)", &UseSnap);
	if (UseSnap)
	{
		if (GizmoOperation == ImGuizmo::TRANSLATE)
			ImGui::DragFloat3("Move snap", TranslationSnap, 0.1f, 0.1f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		else if (GizmoOperation == ImGuizmo::ROTATE)
			ImGui::DragFloat("Rotate snap (deg)", &RotationSnap, 1.0f, 1.0f, 90.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		else
			ImGui::DragFloat3("Scale snap", ScaleSnap, 0.01f, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
}

void jPlacementTool::SetSceneFilter(const char* filter)
{
	if (filter != SceneFilter.InputBuf)
		snprintf(SceneFilter.InputBuf, sizeof(SceneFilter.InputBuf), "%s", filter ? filter : "");
	SceneFilter.Build();
	// This bundled ImGui version returns on the first positive match. Evaluate
	// exclusions first so "cube,-[0]" and "-[0],cube" behave consistently.
	if (SceneFilter.Filters.Size > 1)
		std::stable_partition(SceneFilter.Filters.begin(), SceneFilter.Filters.end(),
			[](const ImGuiTextFilter::ImGuiTextRange& range) { return !range.empty() && *range.b == '-'; });
	SceneListDirty = true;
	RevealSceneSelection = false;
}

void jPlacementTool::RebuildSceneList()
{
	if (!SceneListDirty && SceneObjectLabels.size() == PlacedObjects.size())
		return;
	SceneObjectLabels.clear();
	SceneListIndices.clear();
	SceneObjectLabels.reserve(PlacedObjects.size());
	SceneListIndices.reserve(PlacedObjects.size());
	for (int32 i = 0; i < static_cast<int32>(PlacedObjects.size()); ++i)
	{
		const auto& info = PlacedObjects[i];
		const char* type = "Scene Object";
		if (info.Type == EPlacedObjectType::LIGHT)
		{
			type = info.LightType == EPlacementLightType::POINT ? "Point Light"
				: info.LightType == EPlacementLightType::SPOT ? "Spot Light"
				: info.LightType == EPlacementLightType::DIRECTIONAL ? "Directional Light" : "Light";
		}
		else if (info.ShapeType == EPlacementShapeType::CUBE)
			type = "Cube";
		else if (info.ShapeType == EPlacementShapeType::SPHERE)
			type = "Sphere";
		char label[128];
		if (info.Object)
			sprintf_s(label, "[%d] %s (ID %u)", i, type, info.Object->GetObjectID());
		else
			sprintf_s(label, "[%d] %s", i, type);
		SceneObjectLabels.emplace_back(label);
		if (SceneFilter.PassFilter(label))
			SceneListIndices.push_back(i);
	}
	SceneListDirty = false;
}

const std::vector<int32>& jPlacementTool::GetSceneListIndices()
{
	RebuildSceneList();
	return SceneListIndices;
}

void jPlacementTool::MoveSceneSelection(int32 direction)
{
	const auto& indices = GetSceneListIndices();
	if (indices.empty())
		return;
	const auto found = std::find(indices.begin(), indices.end(), SelectedPlacedObjectIndex);
	const int32 current = found == indices.end() ? (direction > 0 ? -1 : static_cast<int32>(indices.size()))
		: static_cast<int32>(std::distance(indices.begin(), found));
	const int32 next = std::clamp(current + direction, 0, static_cast<int32>(indices.size()) - 1);
	SelectPlacedObject(indices[next]);
}

void jPlacementTool::RenderPlacedObjectsList(jCamera* mainCamera)
{
	ImGui::TextUnformatted("Scene");
	ImGui::SetNextItemWidth(-58);
	if (ImGui::InputTextWithHint("##SceneFilter", "Search type or object ID (comma = OR, -term = exclude)",
		SceneFilter.InputBuf, sizeof(SceneFilter.InputBuf)))
		SetSceneFilter(SceneFilter.InputBuf);
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear"))
		SetSceneFilter("");
	const auto& indices = GetSceneListIndices();
	ImGui::TextDisabled("%d of %d objects | Up/Down select | Esc clear | Delete remove",
		static_cast<int>(indices.size()), static_cast<int>(PlacedObjects.size()));

	ImGui::BeginChild("PlacementListRegion", ImVec2(0, 170), true);
	SceneListHasFocus = ImGui::IsWindowFocused();
	if (indices.empty())
		ImGui::TextDisabled(PlacedObjects.empty() ? "Use Add above to create an object."
			: "No matching objects. Clear or change the search.");
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(indices.size()));
	if (RevealSceneSelection)
	{
		const auto selected = std::find(indices.begin(), indices.end(), SelectedPlacedObjectIndex);
		if (selected != indices.end())
		{
			const int row = static_cast<int>(std::distance(indices.begin(), selected));
			clipper.ForceDisplayRangeByIndices(row, row + 1);
		}
	}
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			const int32 index = indices[row];
			ImGui::PushID(index);
			if (ImGui::Selectable(SceneObjectLabels[index].c_str(), SelectedPlacedObjectIndex == index))
				SelectPlacedObject(index);
			if (RevealSceneSelection && SelectedPlacedObjectIndex == index)
				ImGui::SetScrollHereY();
			ImGui::PopID();
		}
	}
	RevealSceneSelection = false;
	ImGui::EndChild();
	ImGui::Separator();
	ImGui::TextUnformatted("Inspector");
	if (SelectedPlacedObjectIndex >= 0 && SelectedPlacedObjectIndex < static_cast<int32>(PlacedObjects.size()))
	{
		ImGui::TextUnformatted(SceneObjectLabels[SelectedPlacedObjectIndex].c_str());
		if (std::find(indices.begin(), indices.end(), SelectedPlacedObjectIndex) == indices.end())
			ImGui::TextDisabled("Selection is hidden by the search.");
		RenderGizmoControls();
		RenderSelectedObjectProperties(mainCamera);
	}
	else
		ImGui::TextDisabled("Select an object in the scene or click it in the viewport.");
}

void jPlacementTool::RenderSelectedObjectProperties(jCamera* mainCamera)
{
	// Edit selected object
	if (SelectedPlacedObjectIndex >= 0 && SelectedPlacedObjectIndex < (int)PlacedObjects.size())
	{
		const auto& selectedObjectInfo = PlacedObjects[SelectedPlacedObjectIndex];

		// Render 3D Gizmo in viewport
		if (mainCamera && selectedObjectInfo.Object)
		{
			// Setup viewport for Gizmo rendering
			ImGuizmo::Enable(true);
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
			ImGuizmo::SetRect(0, 0, (float)SCR_WIDTH, (float)SCR_HEIGHT);

			// Get camera matrices
			float* viewMatrix = &mainCamera->View.m[0][0];
			float* projMatrix = &mainCamera->Projection.m[0][0];

			// Build transform matrix
			Matrix objectTransform = Matrix(IdentityType);
			Vector objectPos = Vector::ZeroVector;
			Vector objectDir = Vector(0, -1, 0);

			// Get position and direction based on type
			if (selectedObjectInfo.Type == EPlacedObjectType::LIGHT)
			{
				jLight* selectedLight = selectedObjectInfo.LightPtr;
				if (!selectedLight)
					return;

				if (selectedLight->GetLightType() == ELightType::POINT)
				{
					jPointLight* pointLight = static_cast<jPointLight*>(selectedLight);
					objectPos = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData()).Position;
				}
				else if (selectedLight->GetLightType() == ELightType::SPOT)
				{
					jSpotLight* spotLight = static_cast<jSpotLight*>(selectedLight);
					auto& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
					objectPos = data.Position;
					objectDir = data.Direction;
				}
				else if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
				{
					jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(selectedLight);
					objectDir = const_cast<jDirectionalLightUniformBufferData&>(dirLight->GetLightData()).Direction;

					// Use debug object position
					if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
					{
						// If a specific RenderObject is selected, use its position
						if (SelectedRenderObjectIndex >= 0 && SelectedRenderObjectIndex < (int)selectedObjectInfo.Object->RenderObjects.size())
						{
							objectPos = selectedObjectInfo.Object->RenderObjects[SelectedRenderObjectIndex]->GetPos();
						}
						else
						{
							// Otherwise use first RenderObject as representative
							objectPos = selectedObjectInfo.Object->RenderObjects[0]->GetPos();
						}
					}
					else
					{
						objectPos = mainCamera->Pos + objectDir * -100.0f;  // Fallback
					}
				}

				// Create transform matrix (translation + rotation for directional lights)
				if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
				{
					// Build rotation matrix from direction vector
					Vector forward = objectDir;
					if (!IsValidVector(forward) || forward.IsNearlyZero())
					{
						// Fallback to default forward direction
						forward = Vector(0, -1, 0);
					}
					else
					{
						forward = forward.GetNormalize();
					}

					Vector up = Vector(0, 1, 0);

					// More robust parallel vector check with multiple fallbacks
					float dotProduct = fabsf(forward.DotProduct(up));
					if (dotProduct > 0.999f)  // Nearly parallel
					{
						up = Vector(0, 0, 1);
						// Check again with new up vector
						dotProduct = fabsf(forward.DotProduct(up));
						if (dotProduct > 0.999f)  // Still parallel
						{
							up = Vector(1, 0, 0);  // Last resort
						}
					}

					// Compute right vector and validate
					Vector right = up.CrossProduct(forward);
					if (right.IsNearlyZero())
					{
						// Cross product failed - vectors are parallel
						JASSERT("Invalid rotation matrix - cross product is zero");
						// Use identity rotation as fallback
						objectTransform = Matrix::MakeTranslate(objectPos);
					}
					else
					{
						right = right.GetNormalize();

						// Recompute up vector and validate
						up = forward.CrossProduct(right);
						if (up.IsNearlyZero())
						{
							// Second cross product failed
							JASSERT("Invalid rotation matrix - second cross product is zero");
							// Use identity rotation as fallback
							objectTransform = Matrix::MakeTranslate(objectPos);
						}
						else
						{
							up = up.GetNormalize();

							// Build rotation matrix
							Matrix rotationMatrix = Matrix(IdentityType);
							rotationMatrix.m[0][0] = right.x;
							rotationMatrix.m[0][1] = right.y;
							rotationMatrix.m[0][2] = right.z;
							rotationMatrix.m[1][0] = up.x;
							rotationMatrix.m[1][1] = up.y;
							rotationMatrix.m[1][2] = up.z;
							rotationMatrix.m[2][0] = forward.x;
							rotationMatrix.m[2][1] = forward.y;
							rotationMatrix.m[2][2] = forward.z;

							Matrix translationMatrix = Matrix::MakeTranslate(objectPos);
							objectTransform = translationMatrix * rotationMatrix;
						}
					}
				}
				else
				{
					objectTransform = Matrix::MakeTranslate(objectPos);
				}
			}
			else if (selectedObjectInfo.Type == EPlacedObjectType::SHAPE)
			{
				// Get shape transform (position, rotation, scale)
				if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
				{
					jRenderObject* targetRenderObj = nullptr;
					Vector objectRot = Vector::ZeroVector;
					Vector objectScale = Vector::OneVector;

					// Determine which RenderObjects to use for gizmo position
					if (SelectedRenderObjectIndex >= 0 && SelectedRenderObjectIndex < (int)selectedObjectInfo.Object->RenderObjects.size())
					{
						// Single selection: use that RenderObject
						targetRenderObj = selectedObjectInfo.Object->RenderObjects[SelectedRenderObjectIndex];
						objectPos = targetRenderObj->GetPos();
						objectRot = targetRenderObj->GetRot();
						objectScale = targetRenderObj->GetScale();
					}
					else if (!SelectedRenderObjectIndices.empty())
					{
						// Multi-selection: use average position of selected RenderObjects
						Vector avgPos = Vector::ZeroVector;
						int validCount = 0;

						for (int idx : SelectedRenderObjectIndices)
						{
							if (idx >= 0 && idx < (int)selectedObjectInfo.Object->RenderObjects.size())
							{
								avgPos += selectedObjectInfo.Object->RenderObjects[idx]->GetPos();
								validCount++;
							}
						}

						if (validCount > 0)
						{
							objectPos = avgPos / (float)validCount;
							// Use first selected object's rotation and scale for reference
							int firstIdx = SelectedRenderObjectIndices[0];
							if (firstIdx >= 0 && firstIdx < (int)selectedObjectInfo.Object->RenderObjects.size())
							{
								objectRot = selectedObjectInfo.Object->RenderObjects[firstIdx]->GetRot();
								objectScale = selectedObjectInfo.Object->RenderObjects[firstIdx]->GetScale();
							}
						}
					}
					else
					{
						// No selection: use average position of ALL RenderObjects
						Vector avgPos = Vector::ZeroVector;
						for (auto* renderObj : selectedObjectInfo.Object->RenderObjects)
						{
							avgPos += renderObj->GetPos();
						}
						objectPos = avgPos / (float)selectedObjectInfo.Object->RenderObjects.size();

						// Use first RenderObject's rotation and scale for reference
						objectRot = selectedObjectInfo.Object->RenderObjects[0]->GetRot();
						objectScale = selectedObjectInfo.Object->RenderObjects[0]->GetScale();
					}

					// Build transform matrix
					auto posMatrix = Matrix::MakeTranslate(objectPos);
					auto rotMatrix = Matrix::MakeRotate(objectRot);
					auto scaleMatrix = Matrix::MakeScale(objectScale);
					objectTransform = posMatrix * rotMatrix * scaleMatrix;
				}
			}

			// Apply Gizmo manipulation
			float* matrixPtr = &objectTransform.m[0][0];
			float* snapPtr = UseSnap ? (GizmoOperation == ImGuizmo::TRANSLATE ? TranslationSnap :
										GizmoOperation == ImGuizmo::ROTATE ? &RotationSnap :
										ScaleSnap) : nullptr;

			if (ImGuizmo::Manipulate(viewMatrix, projMatrix, GizmoOperation, GizmoMode,
									  matrixPtr, nullptr, snapPtr))
			{
				// Extract transform components from matrix
				float translation[3], rotation[3], scale[3];
				ImGuizmo::DecomposeMatrixToComponents(matrixPtr, translation, rotation, scale);

				// Validate decomposed transform values
				bool isValid = true;
				for (int i = 0; i < 3; ++i)
				{
					if (!IsValidFloat(translation[i]) || !IsValidFloat(rotation[i]) || !IsValidScale(scale[i]))
					{
						isValid = false;
						break;
					}
				}

				// If validation failed, log error and skip this frame's transform
				if (!isValid)
				{
					JASSERT("Invalid transform detected from ImGuizmo - NaN or invalid values");
#ifdef _DEBUG
					char errorMsg[256];
					sprintf_s(errorMsg, "Invalid transform: Pos(%.3f, %.3f, %.3f) Rot(%.3f, %.3f, %.3f) Scale(%.3f, %.3f, %.3f)",
						translation[0], translation[1], translation[2],
						rotation[0], rotation[1], rotation[2],
						scale[0], scale[1], scale[2]);
					OutputDebugStringA(errorMsg);
#endif
					return;  // Skip this transform update
				}

				Vector newPos(translation[0], translation[1], translation[2]);
				const Vector rotationDegrees(rotation[0], rotation[1], rotation[2]);
				Vector newRot(DegreeToRadian(rotation[0]), DegreeToRadian(rotation[1]), DegreeToRadian(rotation[2]));
				Vector newScale(scale[0], scale[1], scale[2]);

				// Additional safety: Clamp scale to prevent extremely small values
				newScale = ClampScale(newScale);

				// Apply to object based on type
				if (selectedObjectInfo.Type == EPlacedObjectType::LIGHT)
				{
					jLight* selectedLight = selectedObjectInfo.LightPtr;
					if (!selectedLight)
						return;

					if (selectedLight->GetLightType() == ELightType::POINT)
					{
						jPointLight* pointLight = static_cast<jPointLight*>(selectedLight);
						auto& data = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData());
						data.Position = newPos;
						pointLight->IsNeedToUpdateShaderBindingInstance = true;

						// Update debug object position
						if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
						{
							selectedObjectInfo.Object->RenderObjects[0]->SetPos(newPos);
						}
					}
					else if (selectedLight->GetLightType() == ELightType::SPOT)
					{
						jSpotLight* spotLight = static_cast<jSpotLight*>(selectedLight);
						auto& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
						data.Position = newPos;
						spotLight->IsNeedToUpdateShaderBindingInstance = true;

						// Update debug object position
						if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
						{
							selectedObjectInfo.Object->RenderObjects[0]->SetPos(newPos);
						}
					}
					else if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
					{
						jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(selectedLight);

						// For rotation: extract new direction from rotation matrix
						if (GizmoOperation == ImGuizmo::ROTATE)
						{
							Vector newForward(objectTransform.m[2][0], objectTransform.m[2][1], objectTransform.m[2][2]);
							newForward = newForward.GetNormalize();
							dirLight->SetDirection(newForward);
						}
						else // Translation: move debug object
						{
							if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
							{
								selectedObjectInfo.Object->RenderObjects[0]->SetPos(newPos);
							}
						}
					}
				}
				else if (selectedObjectInfo.Type == EPlacedObjectType::SHAPE)
				{
					// Update shape transform (position, rotation, scale)
					if (selectedObjectInfo.Object)
					{
						// Single selection: only update that one RenderObject
						if (ApplySelectedTransform(newPos, rotationDegrees, newScale))
						{
							// The same validated degrees-to-radians path serves the inspector.
						}
						// Multi-selection: update all selected RenderObjects
						else if (!SelectedRenderObjectIndices.empty())
						{
							for (int idx : SelectedRenderObjectIndices)
							{
								if (idx >= 0 && idx < (int)selectedObjectInfo.Object->RenderObjects.size())
								{
									jRenderObject* renderObj = selectedObjectInfo.Object->RenderObjects[idx];
									renderObj->SetPos(newPos);
									renderObj->SetRot(newRot);
									renderObj->SetScale(newScale);
								}
							}
						}
						// No selection: apply to ALL RenderObjects
						else
						{
							for (auto* renderObj : selectedObjectInfo.Object->RenderObjects)
							{
								renderObj->SetPos(newPos);
								renderObj->SetRot(newRot);
								renderObj->SetScale(newScale);
							}
						}
					}
				}
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("Drag values or Ctrl+click to type. Rotation is in degrees.");

		// Helper for copy/paste context menu
		auto AddCopyPasteContextMenu = [](const char* contextId, Vector& value, auto&& onPaste)
		{
			if (ImGui::BeginPopupContextItem(contextId))
			{
				if (ImGui::MenuItem("Copy"))
				{
					char buffer[128];
					sprintf_s(buffer, "%.6f, %.6f, %.6f", value.x, value.y, value.z);
					ImGui::SetClipboardText(buffer);
				}
				const char* clipText = ImGui::GetClipboardText();
				if (clipText && strlen(clipText) > 0)
				{
					if (ImGui::MenuItem("Paste"))
					{
						float x, y, z;
						if (sscanf_s(clipText, "%f, %f, %f", &x, &y, &z) == 3 || sscanf_s(clipText, "%f,%f,%f", &x, &y, &z) == 3)
						{
							if (IsValidVector(Vector(x, y, z)))
								onPaste(x, y, z);
						}
					}
				}
				ImGui::EndPopup();
			}
		};

		// Display properties based on type
		if (selectedObjectInfo.Type == EPlacedObjectType::LIGHT)
		{
			jLight* selectedLight = selectedObjectInfo.LightPtr;
			if (!selectedLight)
				return;

			// Position (for point and spot lights)
			if (selectedLight->GetLightType() == ELightType::POINT)
			{
				jPointLight* pointLight = static_cast<jPointLight*>(selectedLight);
				jPointLightUniformBufferData& data = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData());
				Vector pos = data.Position;
				if (ImGui::DragFloat3("Position", &pos.x, 0.5f) && IsValidVector(pos))
				{
					data.Position = pos;
					pointLight->IsNeedToUpdateShaderBindingInstance = true;
				}
				AddCopyPasteContextMenu("PlacedObjectPosContext", pos, [pointLight](float x, float y, float z) {
					Vector newPos(x, y, z);
					jPointLightUniformBufferData& data = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData());
					data.Position = newPos;
					pointLight->IsNeedToUpdateShaderBindingInstance = true;
				});
			}
			else if (selectedLight->GetLightType() == ELightType::SPOT)
			{
				jSpotLight* spotLight = static_cast<jSpotLight*>(selectedLight);
				jSpotLightUniformBufferData& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
				Vector pos = data.Position;
				if (ImGui::DragFloat3("Position", &pos.x, 0.5f) && IsValidVector(pos))
				{
					data.Position = pos;
					spotLight->IsNeedToUpdateShaderBindingInstance = true;
				}
				AddCopyPasteContextMenu("PlacedObjectPosContext", pos, [spotLight](float x, float y, float z) {
					Vector newPos(x, y, z);
					jSpotLightUniformBufferData& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
					data.Position = newPos;
					spotLight->IsNeedToUpdateShaderBindingInstance = true;
				});

				Vector dir = data.Direction;
				if (ImGui::DragFloat3("Direction", &dir.x, 0.01f) && IsValidVector(dir) && !dir.IsNearlyZero())
				{
					dir = dir.GetNormalize();
					spotLight->SetDirection(dir);
				}
				AddCopyPasteContextMenu("PlacedObjectDirContext", dir, [spotLight](float x, float y, float z) {
					Vector newDir(x, y, z);
					if (!newDir.IsNearlyZero())
						spotLight->SetDirection(newDir.GetNormalize());
				});
			}
			else if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
			{
				jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(selectedLight);
				jDirectionalLightUniformBufferData& data = const_cast<jDirectionalLightUniformBufferData&>(dirLight->GetLightData());
				Vector dir = data.Direction;
				if (ImGui::DragFloat3("Direction", &dir.x, 0.01f) && IsValidVector(dir) && !dir.IsNearlyZero())
				{
					dir = dir.GetNormalize();
					dirLight->SetDirection(dir);
				}
				AddCopyPasteContextMenu("PlacedObjectDirContext", dir, [dirLight](float x, float y, float z) {
					Vector newDir(x, y, z);
					if (!newDir.IsNearlyZero())
						dirLight->SetDirection(newDir.GetNormalize());
				});
			}

			// Color
			Vector colorRGB;
			if (selectedLight->GetLightType() == ELightType::POINT)
			{
				jPointLight* pointLight = static_cast<jPointLight*>(selectedLight);
				jPointLightUniformBufferData& data = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData());
				colorRGB = data.Color;
				if (ImGui::ColorEdit3("Color", &colorRGB.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR) && IsValidVector(colorRGB))
				{
					data.Color = colorRGB;
					pointLight->IsNeedToUpdateShaderBindingInstance = true;
				}
				AddCopyPasteContextMenu("PlacedObjectColorContext", colorRGB, [pointLight](float x, float y, float z) {
					jPointLightUniformBufferData& data = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData());
					data.Color = Vector(x, y, z);
					pointLight->IsNeedToUpdateShaderBindingInstance = true;
				});
			}
			else if (selectedLight->GetLightType() == ELightType::SPOT)
			{
				jSpotLight* spotLight = static_cast<jSpotLight*>(selectedLight);
				jSpotLightUniformBufferData& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
				colorRGB = data.Color;
				if (ImGui::ColorEdit3("Color", &colorRGB.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR) && IsValidVector(colorRGB))
				{
					data.Color = colorRGB;
					spotLight->IsNeedToUpdateShaderBindingInstance = true;
				}
				AddCopyPasteContextMenu("PlacedObjectColorContext", colorRGB, [spotLight](float x, float y, float z) {
					jSpotLightUniformBufferData& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
					data.Color = Vector(x, y, z);
					spotLight->IsNeedToUpdateShaderBindingInstance = true;
				});
			}
			else if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
			{
				jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(selectedLight);
				jDirectionalLightUniformBufferData& data = const_cast<jDirectionalLightUniformBufferData&>(dirLight->GetLightData());
				colorRGB = data.Color;
				if (ImGui::ColorEdit3("Color", &colorRGB.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR) && IsValidVector(colorRGB))
				{
					dirLight->SetColor(colorRGB);
				}
				AddCopyPasteContextMenu("PlacedObjectColorContext", colorRGB, [dirLight](float x, float y, float z) {
					dirLight->SetColor(Vector(x, y, z));
				});
			}
		}
		else if (selectedObjectInfo.Type == EPlacedObjectType::SHAPE && selectedObjectInfo.Object)
		{
			const auto& meshes = selectedObjectInfo.Object->RenderObjects;
			if (meshes.size() > 1)
			{
				ImGui::Text("Meshes: %d (Shift+click to select several)", static_cast<int>(meshes.size()));
				if (ImGui::Button("Select all meshes"))
				{
					SelectedRenderObjectIndices.clear();
					for (int32 i = 0; i < static_cast<int32>(meshes.size()); ++i)
						SelectedRenderObjectIndices.push_back(i);
					SelectedRenderObjectIndex = -1;
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear meshes"))
				{
					SelectedRenderObjectIndices.clear();
					SelectedRenderObjectIndex = -1;
				}
				ImGui::BeginChild("RenderObjectListRegion", ImVec2(0, 100), true);
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(meshes.size()));
				while (clipper.Step())
				{
					for (int32 i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
					{
						char label[64];
						sprintf_s(label, "Mesh [%d]", i);
						const bool selected = SelectedRenderObjectIndex == i || IsIndexSelected(SelectedRenderObjectIndices, i);
						if (ImGui::Selectable(label, selected))
						{
							if (ImGui::GetIO().KeyShift)
							{
								if (SelectedRenderObjectIndex >= 0)
									AddIndexToSelection(SelectedRenderObjectIndices, SelectedRenderObjectIndex);
								ToggleIndexInSelection(SelectedRenderObjectIndices, i);
								SelectedRenderObjectIndex = -1;
							}
							else
							{
								SelectedRenderObjectIndex = i;
								SelectedRenderObjectIndices.clear();
							}
						}
					}
				}
				ImGui::EndChild();
			}

			int32 meshIndex = SelectedRenderObjectIndex;
			if (meshIndex < 0 && SelectedRenderObjectIndices.size() == 1)
				meshIndex = SelectedRenderObjectIndices[0];
			if (meshIndex < 0 && SelectedRenderObjectIndices.empty() && meshes.size() == 1)
				meshIndex = 0;
			if (meshIndex >= 0 && meshIndex < static_cast<int32>(meshes.size()) && meshes[meshIndex])
			{
				auto* target = meshes[meshIndex];
				Vector pos = target->GetPos();
				const auto radians = target->GetRot();
				Vector degrees(RadianToDegree(radians.x), RadianToDegree(radians.y), RadianToDegree(radians.z));
				Vector scale = target->GetScale();
				if (ImGui::DragFloat3("Position", &pos.x, 0.5f))
					ApplySelectedTransform(pos, degrees, scale);
				AddCopyPasteContextMenu("PlacedObjectPosContext", pos, [&](float x, float y, float z) {
					ApplySelectedTransform(Vector(x, y, z), degrees, scale);
				});
				if (ImGui::DragFloat3("Rotation (deg)", &degrees.x, 0.5f))
					ApplySelectedTransform(pos, degrees, scale);
				AddCopyPasteContextMenu("PlacedObjectRotContext", degrees, [&](float x, float y, float z) {
					ApplySelectedTransform(pos, Vector(x, y, z), scale);
				});
				if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.001f, 0.0f))
					ApplySelectedTransform(pos, degrees, scale);
				AddCopyPasteContextMenu("PlacedObjectScaleContext", scale, [&](float x, float y, float z) {
					ApplySelectedTransform(pos, degrees, Vector(x, y, z));
				});
			}
			else if (meshes.empty())
				ImGui::TextDisabled("This object has no editable mesh.");
			else
				ImGui::TextWrapped("Select one mesh for numeric transforms. The gizmo applies to the selected meshes, or the whole object when none is selected.");
		}

		// Delete button
		ImGui::Separator();
		if (ImGui::Button("Delete Selected Object"))
		{
			DeletePlacedObject(SelectedPlacedObjectIndex);
		}
	}
}

void jPlacementTool::Clear()
{
	PlacedObjects.clear();
	SetSceneFilter("");
	SceneObjectLabels.clear();
	SceneListIndices.clear();
	SceneListHasFocus = false;
	SelectObject(static_cast<jObject*>(nullptr));
	bPickRequested = false;
	CurrentTab = EPlacementTab::LIGHT;
	SelectedLightType = EPlacementLightType::POINT;
	SelectedShapeType = EPlacementShapeType::CUBE;
}

void jPlacementTool::RegisterStaticObject(jObject* obj)
{
	// Called from jObject::AddObject to register static objects
	// This allows all static objects to appear in the placement list
	if (!obj)
		return;

	// Add to PlacedObjects if not already present
	if (std::find_if(PlacedObjects.begin(), PlacedObjects.end(),
		[obj](const PlacedObjectInfo& info) { return info.Object == obj; }) == PlacedObjects.end())
	{
		PlacedObjectInfo info;
		info.Object = obj;
		info.Type = EPlacedObjectType::SHAPE;  // Static objects are shapes
		PlacedObjects.push_back(info);
		SceneListDirty = true;
	}
}

void jPlacementTool::UnregisterStaticObject(jObject* obj)
{
	// Called from jObject::RemoveObject to unregister static objects
	if (!obj)
		return;

	auto it = std::find_if(PlacedObjects.begin(), PlacedObjects.end(),
		[obj](const PlacedObjectInfo& info) { return info.Object == obj; });

	if (it != PlacedObjects.end())
	{
		int32 index = static_cast<int32>(std::distance(PlacedObjects.begin(), it));
		if (index == SelectedPlacedObjectIndex)
			SelectObject(static_cast<jObject*>(nullptr));
		else if (index < SelectedPlacedObjectIndex)
			SelectedPlacedObjectIndex--;

		PlacedObjects.erase(it);
		SceneListDirty = true;
	}
}

void jPlacementTool::RegisterLight(jLight* light)
{
	if (!light)
		return;

	if (std::find_if(PlacedObjects.begin(), PlacedObjects.end(),
		[light](const PlacedObjectInfo& info) { return info.LightPtr == light; }) != PlacedObjects.end())
	{
		return;
	}

	PlacedObjectInfo info;
	info.Type = EPlacedObjectType::LIGHT;
	info.LightType = ToPlacementLightType(light->GetLightType());
	info.LightPtr = light;
	PlacedObjects.push_back(info);
	SceneListDirty = true;
}

void jPlacementTool::UnregisterLight(jLight* light)
{
	if (!light)
		return;

	auto it = std::find_if(PlacedObjects.begin(), PlacedObjects.end(),
		[light](const PlacedObjectInfo& info) { return info.LightPtr == light; });

	if (it != PlacedObjects.end())
	{
		int32 index = static_cast<int32>(std::distance(PlacedObjects.begin(), it));
		if (index == SelectedPlacedObjectIndex)
			SelectObject(static_cast<jObject*>(nullptr));
		else if (index < SelectedPlacedObjectIndex)
			SelectedPlacedObjectIndex--;

		PlacedObjects.erase(it);
		SceneListDirty = true;
	}
}

void jPlacementTool::SelectPlacedObject(int32 index)
{
	bPickRequested = false;
	PickedObject = nullptr;
	SelectedRenderObjectIndex = -1;
	SelectedRenderObjectIndices.clear();
	SelectedPlacedObjectIndex = index >= 0 && index < static_cast<int32>(PlacedObjects.size()) ? index : -1;
	if (SelectedPlacedObjectIndex >= 0)
		PickedObject = PlacedObjects[SelectedPlacedObjectIndex].Object;
	RevealSceneSelection = SelectedPlacedObjectIndex >= 0;
}

void jPlacementTool::SelectObject(jObject* obj)
{
	if (!obj)
	{
		SelectPlacedObject(-1);
		return;
	}
	const auto it = std::find_if(PlacedObjects.begin(), PlacedObjects.end(),
		[obj](const PlacedObjectInfo& info) { return info.Object == obj; });
	SelectPlacedObject(it != PlacedObjects.end() ? static_cast<int32>(std::distance(PlacedObjects.begin(), it)) : -1);
}

bool jPlacementTool::ApplySelectedTransform(const Vector& position, const Vector& rotationDegrees, const Vector& scale)
{
	if (!IsValidVector(position) || !IsValidVector(rotationDegrees) || !IsValidScaleVector(scale)
		|| SelectedPlacedObjectIndex < 0 || SelectedPlacedObjectIndex >= static_cast<int32>(PlacedObjects.size()))
		return false;
	const auto& info = PlacedObjects[SelectedPlacedObjectIndex];
	if (info.Type != EPlacedObjectType::SHAPE || !info.Object || SelectedRenderObjectIndices.size() > 1)
		return false;
	const auto& meshes = info.Object->RenderObjects;
	int32 meshIndex = SelectedRenderObjectIndex;
	if (meshIndex < 0 && SelectedRenderObjectIndices.size() == 1)
		meshIndex = SelectedRenderObjectIndices[0];
	if (meshIndex < 0 && SelectedRenderObjectIndices.empty() && meshes.size() == 1)
		meshIndex = 0;
	if (meshIndex < 0 || meshIndex >= static_cast<int32>(meshes.size()) || !meshes[meshIndex])
		return false;
	auto* target = meshes[meshIndex];
	target->SetPos(position);
	target->SetRot(Vector(DegreeToRadian(rotationDegrees.x), DegreeToRadian(rotationDegrees.y), DegreeToRadian(rotationDegrees.z)));
	target->SetScale(scale);
	return true;
}

void jPlacementTool::SelectObject(jRenderObject* renderObj)
{
	SelectObject(static_cast<jObject*>(nullptr));
	if (!renderObj || !renderObj->Owner)
	{
		SelectedPlacedObjectIndex = -1;
		SelectedRenderObjectIndex = -1;
		return;
	}

	// Get the owner jObject
	jObject* ownerObj = renderObj->Owner;

	// Find the RenderObject index within the owner's RenderObjects array
	auto& renderObjects = ownerObj->RenderObjects;
	auto it = std::find(renderObjects.begin(), renderObjects.end(), renderObj);

	if (it != renderObjects.end())
	{
		SelectObject(ownerObj);
		if (SelectedPlacedObjectIndex >= 0)
			SelectedRenderObjectIndex = static_cast<int32>(std::distance(renderObjects.begin(), it));
	}
	else
	{
		SelectedPlacedObjectIndex = -1;
		SelectedRenderObjectIndex = -1;
	}
}

#endif // ENABLE_EDITOR_FEATURES
