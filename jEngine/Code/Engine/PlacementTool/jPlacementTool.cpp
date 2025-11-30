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

jPlacementTool::jPlacementTool()
{
}

jPlacementTool::~jPlacementTool()
{
}

void jPlacementTool::ProcessInput(float deltaTime, jCamera* mainCamera, float lightColorScale)
{
	if (!mainCamera)
		return;

	ProcessGizmoHotkeys();
	ProcessTypeSelectionHotkeys();
	ProcessPlacementHotkeys(mainCamera, lightColorScale);

	// Delete: Remove selected object
	static bool wasDeletePressed = false;
	if (g_KeyState[VK_DELETE])
	{
		if (!wasDeletePressed && SelectedPlacedObjectIndex >= 0 &&
			SelectedPlacedObjectIndex < static_cast<int32>(PlacedObjects.size()))
		{
			DeletePlacedObject(SelectedPlacedObjectIndex);
		}
		wasDeletePressed = true;
	}
	else
	{
		wasDeletePressed = false;
	}
}

void jPlacementTool::ProcessGizmoHotkeys()
{
	// Gizmo mode switching hotkeys (when not in ImGui window)
	if (!ImGui::GetIO().WantCaptureKeyboard)
	{
		static bool wasWPressed = false;
		static bool wasEPressed = false;
		static bool wasRPressed = false;
		static bool wasQPressed = false;
		static bool wasCtrlPressed = false;

		// W: Translate mode
		if (g_KeyState['w'] || g_KeyState['W'])
		{
			if (!wasWPressed && g_KeyState[VK_SHIFT])  // Shift+W
			{
				GizmoOperation = ImGuizmo::TRANSLATE;
			}
			wasWPressed = true;
		}
		else
		{
			wasWPressed = false;
		}

		// E: Rotate mode
		if (g_KeyState['e'] || g_KeyState['E'])
		{
			if (!wasEPressed && g_KeyState[VK_SHIFT])  // Shift+E
			{
				GizmoOperation = ImGuizmo::ROTATE;
			}
			wasEPressed = true;
		}
		else
		{
			wasEPressed = false;
		}

		// R: Scale mode
		if (g_KeyState['r'] || g_KeyState['R'])
		{
			if (!wasRPressed && g_KeyState[VK_SHIFT])  // Shift+R
			{
				GizmoOperation = ImGuizmo::SCALE;
			}
			wasRPressed = true;
		}
		else
		{
			wasRPressed = false;
		}

		// Q: Toggle Local/World space
		if (g_KeyState['q'] || g_KeyState['Q'])
		{
			if (!wasQPressed)
			{
				GizmoMode = (GizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
			}
			wasQPressed = true;
		}
		else
		{
			wasQPressed = false;
		}

		// Ctrl: Toggle snap
		if (g_KeyState[VK_CONTROL])
		{
			if (!wasCtrlPressed)
			{
				UseSnap = !UseSnap;
			}
			wasCtrlPressed = true;
		}
		else
		{
			wasCtrlPressed = false;
		}
	}
}

void jPlacementTool::ProcessTypeSelectionHotkeys()
{
	// Light type selection hotkeys
	static bool wasLPressed = false;
	static bool wasKPressed = false;
	static bool wasJPressed = false;

	// L: Select Point Light
	if (g_KeyState['l'] || g_KeyState['L'])
	{
		if (!wasLPressed)
		{
			CurrentTab = EPlacementTab::LIGHT;
			SelectedLightType = EPlacementLightType::POINT;
		}
		wasLPressed = true;
	}
	else
	{
		wasLPressed = false;
	}

	// K: Select Spot Light
	if (g_KeyState['k'] || g_KeyState['K'])
	{
		if (!wasKPressed)
		{
			CurrentTab = EPlacementTab::LIGHT;
			SelectedLightType = EPlacementLightType::SPOT;
		}
		wasKPressed = true;
	}
	else
	{
		wasKPressed = false;
	}

	// J: Select Directional Light
	if (g_KeyState['j'] || g_KeyState['J'])
	{
		if (!wasJPressed)
		{
			CurrentTab = EPlacementTab::LIGHT;
			SelectedLightType = EPlacementLightType::DIRECTIONAL;
		}
		wasJPressed = true;
	}
	else
	{
		wasJPressed = false;
	}

	// Shape type selection hotkeys
	static bool wasNPressed = false;
	static bool wasMPressed = false;

	// N: Select Cube
	if (g_KeyState['n'] || g_KeyState['N'])
	{
		if (!wasNPressed)
		{
			CurrentTab = EPlacementTab::SHAPE;
			SelectedShapeType = EPlacementShapeType::CUBE;
		}
		wasNPressed = true;
	}
	else
	{
		wasNPressed = false;
	}

	// M: Select Sphere
	if (g_KeyState['m'] || g_KeyState['M'])
	{
		if (!wasMPressed)
		{
			CurrentTab = EPlacementTab::SHAPE;
			SelectedShapeType = EPlacementShapeType::SPHERE;
		}
		wasMPressed = true;
	}
	else
	{
		wasMPressed = false;
	}
}

void jPlacementTool::ProcessPlacementHotkeys(jCamera* mainCamera, float lightColorScale)
{
	// P: Place selected object
	static bool wasPPressed = false;
	if (g_KeyState['p'] || g_KeyState['P'])
	{
		if (!wasPPressed)
		{
			PlaceSelectedObject(mainCamera, lightColorScale);
		}
		wasPPressed = true;
	}
	else
	{
		wasPPressed = false;
	}
}

void jPlacementTool::PlaceSelectedObject(jCamera* mainCamera, float lightColorScale)
{
	if (!mainCamera)
		return;

	if (CurrentTab == EPlacementTab::LIGHT)
	{
		PlaceLight(SelectedLightType, mainCamera, lightColorScale);
	}
	else if (CurrentTab == EPlacementTab::SHAPE)
	{
		PlaceShape(SelectedShapeType, mainCamera);
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
	jObject::AddObject(cube);

	// Add to placed objects
	PlacedObjectInfo info;
	info.Object = cube;
	info.Type = EPlacedObjectType::SHAPE;
	info.ShapeType = EPlacementShapeType::CUBE;
	PlacedObjects.push_back(info);
}

void jPlacementTool::CreateSphere(const Vector& position)
{
	float radius = 15.0f;
	uint32 slices = 32;
	uint32 stacks = 16;
	Vector scale = Vector::OneVector;
	Vector4 color = Vector4(0.7f, 0.7f, 0.7f, 1.0f);

	auto* sphere = jPrimitiveUtil::CreateSphere(position, radius, slices, stacks, scale, color, false, true);
	jObject::AddObject(sphere);

	// Add to placed objects
	PlacedObjectInfo info;
	info.Object = sphere;
	info.Type = EPlacedObjectType::SHAPE;
	info.ShapeType = EPlacementShapeType::SPHERE;
	PlacedObjects.push_back(info);
}

void jPlacementTool::DeletePlacedObject(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(PlacedObjects.size()))
		return;

	const auto& objectInfo = PlacedObjects[index];

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
			jObject::RemoveObject(objectInfo.Object);
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

	// Remove from list
	PlacedObjects.erase(PlacedObjects.begin() + index);

	// Reset selection
	SelectedPlacedObjectIndex = -1;
}

void jPlacementTool::RenderUI(jCamera* mainCamera)
{
	// Initialize ImGuizmo for this frame
	ImGuizmo::BeginFrame();

	// Render vertical tab bar
	RenderTabBar();

	ImGui::Separator();

	// Render selected tab content
	if (CurrentTab == EPlacementTab::LIGHT)
	{
		RenderLightTab(mainCamera, 1.0f);
	}
	else if (CurrentTab == EPlacementTab::SHAPE)
	{
		RenderShapeTab(mainCamera);
	}

	ImGui::Separator();

	// Common sections (shared between Light and Shape)
	RenderGizmoControls();
	ImGui::Separator();
	RenderPlacedObjectsList(mainCamera);
}

void jPlacementTool::RenderTabBar()
{
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Object Types:");

	// Vertical tab buttons
	ImGui::BeginGroup();
	{
		if (ImGui::Selectable("Light", CurrentTab == EPlacementTab::LIGHT, 0, ImVec2(120, 30)))
		{
			CurrentTab = EPlacementTab::LIGHT;
		}
		if (ImGui::Selectable("Shape", CurrentTab == EPlacementTab::SHAPE, 0, ImVec2(120, 30)))
		{
			CurrentTab = EPlacementTab::SHAPE;
			// Auto-select first shape (Cube) when switching to Shape tab
			if (SelectedShapeType == EPlacementShapeType::NONE)
			{
				SelectedShapeType = EPlacementShapeType::CUBE;
			}
		}
	}
	ImGui::EndGroup();
}

void jPlacementTool::RenderLightTab(jCamera* mainCamera, float lightColorScale)
{
	ImGui::TextColored(ImVec4(0, 1, 1, 1), "Light Type Selection:");

	ImGui::Indent();

	// Light type selection
	if (ImGui::RadioButton("Point Light (L)", SelectedLightType == EPlacementLightType::POINT))
		SelectedLightType = EPlacementLightType::POINT;

	if (ImGui::RadioButton("Spot Light (K)", SelectedLightType == EPlacementLightType::SPOT))
		SelectedLightType = EPlacementLightType::SPOT;

	if (ImGui::RadioButton("Directional Light (J)", SelectedLightType == EPlacementLightType::DIRECTIONAL))
		SelectedLightType = EPlacementLightType::DIRECTIONAL;

	ImGui::Unindent();

	ImGui::Spacing();

	// Hotkeys
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Hotkeys:");
	ImGui::Text("  L/K/J - Select Light Type");
	ImGui::Text("  P - Place Selected Light");
}

void jPlacementTool::RenderShapeTab(jCamera* mainCamera)
{
	ImGui::TextColored(ImVec4(0, 1, 1, 1), "Shape Type Selection:");

	ImGui::Indent();

	// Shape type selection
	if (ImGui::RadioButton("Cube (N)", SelectedShapeType == EPlacementShapeType::CUBE))
		SelectedShapeType = EPlacementShapeType::CUBE;

	if (ImGui::RadioButton("Sphere (M)", SelectedShapeType == EPlacementShapeType::SPHERE))
		SelectedShapeType = EPlacementShapeType::SPHERE;

	ImGui::Unindent();

	ImGui::Spacing();

	// Hotkeys
	ImGui::TextColored(ImVec4(1, 1, 0, 1), "Hotkeys:");
	ImGui::Text("  N/M - Select Shape Type");
	ImGui::Text("  P - Place Selected Shape");
}

void jPlacementTool::RenderGizmoControls()
{
	ImGui::TextColored(ImVec4(0, 1, 1, 1), "Gizmo Controls:");
	ImGui::Text("Operation Mode:");
	ImGui::Indent();
	if (ImGui::RadioButton("Translate (Shift+W)", GizmoOperation == ImGuizmo::TRANSLATE))
		GizmoOperation = ImGuizmo::TRANSLATE;
	if (ImGui::RadioButton("Rotate (Shift+E)", GizmoOperation == ImGuizmo::ROTATE))
		GizmoOperation = ImGuizmo::ROTATE;
	if (ImGui::RadioButton("Scale (Shift+R)", GizmoOperation == ImGuizmo::SCALE))
		GizmoOperation = ImGuizmo::SCALE;
	ImGui::Unindent();

	ImGui::Spacing();
	ImGui::Text("Coordinate Space:");
	ImGui::Indent();
	if (ImGui::RadioButton("Local", GizmoMode == ImGuizmo::LOCAL))
		GizmoMode = ImGuizmo::LOCAL;
	ImGui::SameLine();
	if (ImGui::RadioButton("World (Q)", GizmoMode == ImGuizmo::WORLD))
		GizmoMode = ImGuizmo::WORLD;
	ImGui::Unindent();

	ImGui::Spacing();
	ImGui::Checkbox("Enable Snap (Ctrl)", &UseSnap);

	if (UseSnap)
	{
		ImGui::Indent();
		if (GizmoOperation == ImGuizmo::TRANSLATE)
		{
			ImGui::DragFloat3("Translation Snap", TranslationSnap, 0.1f, 0.1f, 100.0f);
		}
		else if (GizmoOperation == ImGuizmo::ROTATE)
		{
			ImGui::DragFloat("Rotation Snap (deg)", &RotationSnap, 1.0f, 1.0f, 90.0f);
		}
		else if (GizmoOperation == ImGuizmo::SCALE)
		{
			ImGui::DragFloat3("Scale Snap", ScaleSnap, 0.01f, 0.01f, 1.0f);
		}
		ImGui::Unindent();
	}
}

void jPlacementTool::RenderPlacedObjectsList(jCamera* mainCamera)
{
	if (!PlacedObjects.empty())
	{
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Placed Objects: %d", (int)PlacedObjects.size());

		ImGui::BeginChild("PlacementListRegion", ImVec2(0, 150), true);
		for (int i = 0; i < (int)PlacedObjects.size(); ++i)
		{
			const auto& objInfo = PlacedObjects[i];
			char labelBuf[128];

			if (objInfo.Type == EPlacedObjectType::LIGHT)
			{
				const char* lightTypeName = "Unknown";
				if (objInfo.LightType == EPlacementLightType::POINT)
					lightTypeName = "Point";
				else if (objInfo.LightType == EPlacementLightType::SPOT)
					lightTypeName = "Spot";
				else if (objInfo.LightType == EPlacementLightType::DIRECTIONAL)
					lightTypeName = "Directional";

				sprintf_s(labelBuf, "[%d] %s Light", i, lightTypeName);
			}
			else if (objInfo.Type == EPlacedObjectType::SHAPE)
			{
				const char* shapeTypeName = "Unknown";
				if (objInfo.ShapeType == EPlacementShapeType::CUBE)
					shapeTypeName = "Cube";
				else if (objInfo.ShapeType == EPlacementShapeType::SPHERE)
					shapeTypeName = "Sphere";

				sprintf_s(labelBuf, "[%d] %s", i, shapeTypeName);
			}
			else
			{
				sprintf_s(labelBuf, "[%d] Unknown", i);
			}

			if (ImGui::Selectable(labelBuf, SelectedPlacedObjectIndex == i))
			{
				SelectedPlacedObjectIndex = i;
			}
		}
		ImGui::EndChild();

		// Render selected object properties
		RenderSelectedObjectProperties(mainCamera);
	}
	else
	{
		ImGui::Text("No objects placed yet.");
		ImGui::Text("Select a type and press P to place!");
	}
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
						objectPos = selectedObjectInfo.Object->RenderObjects[0]->GetPos();
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
					Vector forward = objectDir.GetNormalize();
					Vector up = Vector(0, 1, 0);

					if (fabsf(forward.DotProduct(up)) > 0.999f)
						up = Vector(0, 0, 1);

					Vector right = up.CrossProduct(forward).GetNormalize();
					up = forward.CrossProduct(right).GetNormalize();

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
				else
				{
					objectTransform = Matrix::MakeTranslate(objectPos);
				}
			}
			else if (selectedObjectInfo.Type == EPlacedObjectType::SHAPE)
			{
				// Get shape position
				if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
				{
					objectPos = selectedObjectInfo.Object->RenderObjects[0]->GetPos();
					objectTransform = Matrix::MakeTranslate(objectPos);
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
				// Extract new position from matrix
				Vector newPos(objectTransform.m[3][0], objectTransform.m[3][1], objectTransform.m[3][2]);

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
					// Update shape position
					if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
					{
						selectedObjectInfo.Object->RenderObjects[0]->SetPos(newPos);
					}
				}
			}
		}

		ImGui::Separator();
		ImGui::TextColored(ImVec4(0, 1, 0, 1), "Selected Object Properties:");

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
				if (ImGui::SliderFloat3("Position", &pos.x, -500.0f, 500.0f))
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
				if (ImGui::SliderFloat3("Position", &pos.x, -500.0f, 500.0f))
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
				if (ImGui::SliderFloat3("Direction", &dir.x, -1.0f, 1.0f))
				{
					dir = dir.GetNormalize();
					spotLight->SetDirection(dir);
				}
				AddCopyPasteContextMenu("PlacedObjectDirContext", dir, [spotLight](float x, float y, float z) {
					Vector newDir(x, y, z);
					newDir = newDir.GetNormalize();
					spotLight->SetDirection(newDir);
				});
			}
			else if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
			{
				jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(selectedLight);
				jDirectionalLightUniformBufferData& data = const_cast<jDirectionalLightUniformBufferData&>(dirLight->GetLightData());
				Vector dir = data.Direction;
				if (ImGui::SliderFloat3("Direction", &dir.x, -1.0f, 1.0f))
				{
					dir = dir.GetNormalize();
					dirLight->SetDirection(dir);
				}
				AddCopyPasteContextMenu("PlacedObjectDirContext", dir, [dirLight](float x, float y, float z) {
					Vector newDir(x, y, z);
					newDir = newDir.GetNormalize();
					dirLight->SetDirection(newDir);
				});
			}

			// Color
			Vector colorRGB;
			if (selectedLight->GetLightType() == ELightType::POINT)
			{
				jPointLight* pointLight = static_cast<jPointLight*>(selectedLight);
				jPointLightUniformBufferData& data = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData());
				colorRGB = data.Color;
				if (ImGui::ColorEdit3("Color", &colorRGB.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
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
				if (ImGui::ColorEdit3("Color", &colorRGB.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
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
				if (ImGui::ColorEdit3("Color", &colorRGB.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
				{
					dirLight->SetColor(colorRGB);
				}
				AddCopyPasteContextMenu("PlacedObjectColorContext", colorRGB, [dirLight](float x, float y, float z) {
					dirLight->SetColor(Vector(x, y, z));
				});
			}
		}
		else if (selectedObjectInfo.Type == EPlacedObjectType::SHAPE)
		{
			// Display shape properties
			if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
			{
				Vector pos = selectedObjectInfo.Object->RenderObjects[0]->GetPos();
				if (ImGui::SliderFloat3("Position", &pos.x, -500.0f, 500.0f))
				{
					selectedObjectInfo.Object->RenderObjects[0]->SetPos(pos);
				}
				AddCopyPasteContextMenu("PlacedObjectPosContext", pos, [&selectedObjectInfo](float x, float y, float z) {
					Vector newPos(x, y, z);
					if (selectedObjectInfo.Object && !selectedObjectInfo.Object->RenderObjects.empty())
					{
						selectedObjectInfo.Object->RenderObjects[0]->SetPos(newPos);
					}
				});
			}
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
	SelectedPlacedObjectIndex = -1;
	CurrentTab = EPlacementTab::LIGHT;
	SelectedLightType = EPlacementLightType::POINT;
	SelectedShapeType = EPlacementShapeType::CUBE;
}

#endif // ENABLE_EDITOR_FEATURES
