#include "pch.h"

#ifdef ENABLE_EDITOR_FEATURES

#include "jEditor.h"
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

jEditor* g_Editor = nullptr;

jEditor::jEditor()
{
}

jEditor::~jEditor()
{
}

void jEditor::PlacementTool::ProcessInput(float deltaTime, jCamera* mainCamera, float lightColorScale)
{
	if (!EnablePlacementMode || !mainCamera)
		return;

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

	// Hotkey states for light creation
	static bool wasLPressed = false;
	static bool wasKPressed = false;
	static bool wasJPressed = false;
	static bool wasDeletePressed = false;

	// L: Create Point Light at camera position
	if (g_KeyState['l'] || g_KeyState['L'])
	{
		if (!wasLPressed)
		{
			Vector cameraPos = mainCamera->Pos;
			Vector lightColor = Vector(1.0f, 1.0f, 1.0f);  // White
			auto* newLight = jLight::CreatePointLight(
				cameraPos,
				lightColor * lightColorScale,
				1500.0f,  // maxDistance
				Vector(1.0f, 1.0f, 1.0f),  // diffuseIntensity
				Vector(1.0f, 1.0f, 1.0f),  // specularIntensity
				64.0f  // specularPower
			);

			jLight::AddLights(newLight);
			PlacedLights.push_back(newLight);

			// Create debug visualization
			Vector scale(1.0f, 1.0f, 1.0f);
			auto* debugObj = jPrimitiveUtil::CreatePointLightDebug(scale, mainCamera, static_cast<jPointLight*>(newLight), "Image/bulb.png");
			PlacedLightDebugObjects.push_back(debugObj->BillboardObject);
		}
		wasLPressed = true;
	}
	else
	{
		wasLPressed = false;
	}

	// K: Create Spot Light at camera position
	if (g_KeyState['k'] || g_KeyState['K'])
	{
		if (!wasKPressed)
		{
			Vector cameraPos = mainCamera->Pos;
			Vector cameraDir = mainCamera->GetForwardVector();
			Vector lightColor = Vector(1.0f, 1.0f, 0.0f);  // Yellow
			auto* newLight = jLight::CreateSpotLight(
				cameraPos,
				cameraDir,
				lightColor * lightColorScale,
				2000.0f,
				0.35f,
				1.0f,
				Vector(1.0f, 1.0f, 1.0f),
				Vector(1.0f),
				64.0f
			);

			jLight::AddLights(newLight);
			PlacedLights.push_back(newLight);

			// Create debug visualization
			Vector scale(1.0f, 1.0f, 1.0f);
			auto* debugObj = jPrimitiveUtil::CreateSpotLightDebug(scale, mainCamera, static_cast<jSpotLight*>(newLight), "Image/spot.png");
			PlacedLightDebugObjects.push_back(debugObj->BillboardObject);
		}
		wasKPressed = true;
	}
	else
	{
		wasKPressed = false;
	}

	// J: Create Directional Light pointing in camera direction
	if (g_KeyState['j'] || g_KeyState['J'])
	{
		if (!wasJPressed)
		{
			Vector lightColor = gOptions.DirectionalLightColor * gOptions.DirectionalLightIntensity;
			auto* newLight = jLight::CreateDirectionalLight(
				gOptions.DefaultSunDir,
				lightColor,
				Vector(1.0f, 1.0f, 1.0f),  // diffuseIntensity
				Vector(1.0f, 1.0f, 1.0f),  // specularIntensity
				64.0f  // specularPower
			);

			jLight::AddLights(newLight);
			PlacedLights.push_back(newLight);

			// Create debug visualization (at camera position for visibility)
			Vector scale(1.0f, 1.0f, 1.0f);
			float length = 50.0f;
			auto* debugObj = jPrimitiveUtil::CreateDirectionalLightDebug(mainCamera->Pos, scale, length, mainCamera, static_cast<jDirectionalLight*>(newLight), "Image/sun.png");
			PlacedLightDebugObjects.push_back(debugObj->BillboardObject);
		}
		wasJPressed = true;
	}
	else
	{
		wasJPressed = false;
	}

	// Delete: Remove selected light
	if (g_KeyState[VK_DELETE])
	{
		if (!wasDeletePressed && SelectedPlacedLightIndex >= 0 &&
			SelectedPlacedLightIndex < static_cast<int32>(PlacedLights.size()))
		{
			DeletePlacedLight(SelectedPlacedLightIndex);
		}
		wasDeletePressed = true;
	}
	else
	{
		wasDeletePressed = false;
	}
}

void jEditor::PlacementTool::DeletePlacedLight(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(PlacedLights.size()))
		return;

	// Remove debug object
	if (index < static_cast<int32>(PlacedLightDebugObjects.size()))
	{
		auto* debugObj = PlacedLightDebugObjects[index];
		jObject::RemoveObject(debugObj);
		PlacedLightDebugObjects.erase(PlacedLightDebugObjects.begin() + index);
	}

	// Remove light
	auto* light = PlacedLights[index];
	jLight::RemoveLights(light);
	PlacedLights.erase(PlacedLights.begin() + index);

	// Reset selection
	SelectedPlacedLightIndex = -1;
}

void jEditor::PlacementTool::RenderUI(jCamera* mainCamera)
{
	// Initialize ImGuizmo for this frame
	ImGuizmo::BeginFrame();

	ImGui::Separator();
	ImGui::TextColored(ImVec4(0, 1, 1, 1), "Placement Tool");

	ImGui::Checkbox("Enable Placement Mode", &EnablePlacementMode);

	if (EnablePlacementMode)
	{
		ImGui::Indent();
		ImGui::TextColored(ImVec4(1, 1, 0, 1), "Hotkeys:");
		ImGui::Text("  L - Create Point Light (White)");
		ImGui::Text("  K - Create Spot Light (Yellow)");
		ImGui::Text("  J - Create Directional Light (Orange)");
		ImGui::Text("  DELETE - Remove selected light");
		ImGui::Separator();

		// Gizmo Controls
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
		ImGui::Separator();

		if (!PlacedLights.empty())
		{
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "Placed Lights: %d", (int)PlacedLights.size());

			ImGui::BeginChild("PlacementListRegion", ImVec2(0, 150), true);
			for (int i = 0; i < (int)PlacedLights.size(); ++i)
			{
				jLight* light = PlacedLights[i];
				const char* lightTypeName = "Unknown";
				if (light->GetLightType() == ELightType::POINT)
					lightTypeName = "Point";
				else if (light->GetLightType() == ELightType::SPOT)
					lightTypeName = "Spot";
				else if (light->GetLightType() == ELightType::DIRECTIONAL)
					lightTypeName = "Directional";

				char labelBuf[128];
				sprintf_s(labelBuf, "[%d] %s Light", i, lightTypeName);

				if (ImGui::Selectable(labelBuf, SelectedPlacedLightIndex == i))
				{
					SelectedPlacedLightIndex = i;
				}
			}
			ImGui::EndChild();

			// Edit selected light
			if (SelectedPlacedLightIndex >= 0 &&
				SelectedPlacedLightIndex < (int)PlacedLights.size())
			{
				jLight* selectedLight = PlacedLights[SelectedPlacedLightIndex];

				// Render 3D Gizmo in viewport
				if (mainCamera)
				{
					// Setup viewport for Gizmo rendering
					ImGuizmo::Enable(true);
					ImGuizmo::SetOrthographic(false);
					ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
					ImGuizmo::SetRect(0, 0, (float)SCR_WIDTH, (float)SCR_HEIGHT);

					// Get camera matrices
					float* viewMatrix = &mainCamera->View.m[0][0];
					float* projMatrix = &mainCamera->Projection.m[0][0];

					// Build transform matrix for selected light
					Matrix lightTransform = Matrix(IdentityType);
					Vector lightPos = Vector::ZeroVector;
					Vector lightDir = Vector(0, -1, 0);

					// Get position and direction based on light type
					if (selectedLight->GetLightType() == ELightType::POINT)
					{
						jPointLight* pointLight = static_cast<jPointLight*>(selectedLight);
						lightPos = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData()).Position;
					}
					else if (selectedLight->GetLightType() == ELightType::SPOT)
					{
						jSpotLight* spotLight = static_cast<jSpotLight*>(selectedLight);
						auto& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
						lightPos = data.Position;
						lightDir = data.Direction;
					}
					else if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
					{
						jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(selectedLight);
						lightDir = const_cast<jDirectionalLightUniformBufferData&>(dirLight->GetLightData()).Direction;

						// Use debug object (BillboardObject) position if available
						if (SelectedPlacedLightIndex < (int)PlacedLightDebugObjects.size())
						{
							auto* debugObj = PlacedLightDebugObjects[SelectedPlacedLightIndex];
							if (debugObj && !debugObj->RenderObjects.empty())
							{
								lightPos = debugObj->RenderObjects[0]->GetPos();
							}
							else
							{
								lightPos = mainCamera->Pos + lightDir * -100.0f;  // Fallback
							}
						}
						else
						{
							lightPos = mainCamera->Pos + lightDir * -100.0f;  // Fallback
						}
					}

					// Create transform matrix (translation + rotation for directional lights)
					if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
					{
						// Build rotation matrix from direction vector
						Vector forward = lightDir.GetNormalize();
						Vector up = Vector(0, 1, 0);

						// Handle case where forward is parallel to up
						if (fabsf(forward.DotProduct(up)) > 0.999f)
							up = Vector(0, 0, 1);

						Vector right = up.CrossProduct(forward).GetNormalize();
						up = forward.CrossProduct(right).GetNormalize();

						// Build rotation matrix from basis vectors
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

						// Combine translation and rotation
						Matrix translationMatrix = Matrix::MakeTranslate(lightPos);
						lightTransform = translationMatrix * rotationMatrix;
					}
					else
					{
						lightTransform = Matrix::MakeTranslate(lightPos);
					}

					// Apply Gizmo manipulation
					float* matrixPtr = &lightTransform.m[0][0];
					float* snapPtr = UseSnap ? (GizmoOperation == ImGuizmo::TRANSLATE ? TranslationSnap :
												GizmoOperation == ImGuizmo::ROTATE ? &RotationSnap :
												ScaleSnap) : nullptr;

					if (ImGuizmo::Manipulate(viewMatrix, projMatrix, GizmoOperation, GizmoMode,
											  matrixPtr, nullptr, snapPtr))
					{
						// Extract new position from matrix
						Vector newPos(lightTransform.m[3][0], lightTransform.m[3][1], lightTransform.m[3][2]);

						// Apply to light based on type
						if (selectedLight->GetLightType() == ELightType::POINT)
						{
							jPointLight* pointLight = static_cast<jPointLight*>(selectedLight);
							auto& data = const_cast<jPointLightUniformBufferData&>(pointLight->GetLightData());
							data.Position = newPos;
							pointLight->IsNeedToUpdateShaderBindingInstance = true;

							// Update debug object position
							if (SelectedPlacedLightIndex < (int)PlacedLightDebugObjects.size())
							{
								auto* debugObj = PlacedLightDebugObjects[SelectedPlacedLightIndex];
								if (debugObj && !debugObj->RenderObjects.empty())
								{
									debugObj->RenderObjects[0]->SetPos(newPos);
								}
							}
						}
						else if (selectedLight->GetLightType() == ELightType::SPOT)
						{
							jSpotLight* spotLight = static_cast<jSpotLight*>(selectedLight);
							auto& data = const_cast<jSpotLightUniformBufferData&>(spotLight->GetLightData());
							data.Position = newPos;
							spotLight->IsNeedToUpdateShaderBindingInstance = true;

							// Update debug object position
							if (SelectedPlacedLightIndex < (int)PlacedLightDebugObjects.size())
							{
								auto* debugObj = PlacedLightDebugObjects[SelectedPlacedLightIndex];
								if (debugObj && !debugObj->RenderObjects.empty())
								{
									debugObj->RenderObjects[0]->SetPos(newPos);
								}
							}
						}
						else if (selectedLight->GetLightType() == ELightType::DIRECTIONAL)
						{
							jDirectionalLight* dirLight = static_cast<jDirectionalLight*>(selectedLight);

							// For rotation: extract new direction from rotation matrix
							if (GizmoOperation == ImGuizmo::ROTATE)
							{
								// Extract rotation matrix and get forward vector (Z-axis)
								Vector newForward(lightTransform.m[2][0], lightTransform.m[2][1], lightTransform.m[2][2]);
								newForward = newForward.GetNormalize();
								dirLight->SetDirection(newForward);

								// Debug object position stays at gizmo position (newPos)
								// Don't recalculate based on camera, just keep it where user placed it
							}
							else // Translation: move debug object
							{
								// Update debug object (BillboardObject) position
								if (SelectedPlacedLightIndex < (int)PlacedLightDebugObjects.size())
								{
									auto* debugObj = PlacedLightDebugObjects[SelectedPlacedLightIndex];
									if (debugObj && !debugObj->RenderObjects.empty())
									{
										debugObj->RenderObjects[0]->SetPos(newPos);
									}
								}
							}
						}
					}
				}

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0, 1, 0, 1), "Selected Light Properties:");

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
					AddCopyPasteContextMenu("PlacedLightPosContext", pos, [pointLight](float x, float y, float z) {
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
					AddCopyPasteContextMenu("PlacedLightPosContext", pos, [spotLight](float x, float y, float z) {
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
					AddCopyPasteContextMenu("PlacedLightDirContext", dir, [spotLight](float x, float y, float z) {
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
					AddCopyPasteContextMenu("PlacedLightDirContext", dir, [this, dirLight](float x, float y, float z) {
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
					AddCopyPasteContextMenu("PlacedLightColorContext", colorRGB, [pointLight](float x, float y, float z) {
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
					AddCopyPasteContextMenu("PlacedLightColorContext", colorRGB, [spotLight](float x, float y, float z) {
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
					AddCopyPasteContextMenu("PlacedLightColorContext", colorRGB, [dirLight](float x, float y, float z) {
						dirLight->SetColor(Vector(x, y, z));
					});
				}

				// Delete button
				ImGui::Separator();
				if (ImGui::Button("Delete Selected Light"))
				{
					DeletePlacedLight(SelectedPlacedLightIndex);
				}
			}
		}
		else
		{
			ImGui::Text("No lights placed yet.");
			ImGui::Text("Press L, K, or J to create lights!");
		}

		ImGui::Unindent();
	}
}

void jEditor::PlacementTool::Clear()
{
	PlacedLights.clear();
	PlacedLightDebugObjects.clear();
	EnablePlacementMode = false;
	SelectedPlacedLightIndex = -1;
}

#endif // ENABLE_EDITOR_FEATURES
