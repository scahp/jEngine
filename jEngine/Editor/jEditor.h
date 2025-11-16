#pragma once

// Editor-specific features that can be excluded from runtime builds
// Define ENABLE_EDITOR_FEATURES to include editor functionality

#ifdef ENABLE_EDITOR_FEATURES

#include "External/ImGuizmo/ImGuizmo.h"

class jLight;
class jObject;
class jCamera;

class jEditor
{
public:
	jEditor();
	~jEditor();

	// Object/Light Placement Tool
	struct PlacementTool
	{
		bool EnablePlacementMode = false;
		int32 SelectedPlacedLightIndex = -1;

		std::vector<jLight*> PlacedLights;
		std::vector<jObject*> PlacedLightDebugObjects;

		// Gizmo settings
		ImGuizmo::OPERATION GizmoOperation = ImGuizmo::TRANSLATE;
		ImGuizmo::MODE GizmoMode = ImGuizmo::WORLD;
		bool UseSnap = false;
		float TranslationSnap[3] = {1.0f, 1.0f, 1.0f};
		float RotationSnap = 15.0f;  // degrees
		float ScaleSnap[3] = {0.1f, 0.1f, 0.1f};

		void ProcessInput(float deltaTime, jCamera* mainCamera, float lightColorScale);
		void DeletePlacedLight(int32 index);
		void RenderUI(jCamera* mainCamera);
		void Clear();
	};

	PlacementTool Placement;

	// Add more editor features here in the future
	// e.g., TerrainEditor, MaterialEditor, etc.
};

extern jEditor* g_Editor;

#endif // ENABLE_EDITOR_FEATURES
