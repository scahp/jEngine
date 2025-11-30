#pragma once

// Editor-specific features that can be excluded from runtime builds
// Define ENABLE_EDITOR_FEATURES to include editor functionality

#ifdef ENABLE_EDITOR_FEATURES

#include "External/ImGuizmo/ImGuizmo.h"

class jLight;
class jObject;
class jCamera;

// Light type selection for placement
enum class EPlacementLightType
{
	NONE,
	POINT,
	SPOT,
	DIRECTIONAL
};

// Shape type selection for placement
enum class EPlacementShapeType
{
	NONE,
	CUBE,
	SPHERE
};

// Vertical tab selection
enum class EPlacementTab
{
	LIGHT,
	SHAPE
};

// Placed object type tracking
enum class EPlacedObjectType
{
	LIGHT,
	SHAPE
};

// Placed object info
struct PlacedObjectInfo
{
	jObject* Object = nullptr;
	EPlacedObjectType Type;
	EPlacementLightType LightType = EPlacementLightType::NONE;
	EPlacementShapeType ShapeType = EPlacementShapeType::NONE;
	jLight* LightPtr = nullptr;  // Only valid if Type == LIGHT
};

class jPlacementTool
{
public:
	jPlacementTool();
	~jPlacementTool();

	// Main Interface
	void ProcessInput(float deltaTime, jCamera* mainCamera, float lightColorScale);
	void RenderUI(jCamera* mainCamera);
	void Clear();

	// Placement Mode
	bool EnablePlacementMode = false;

	// Placed Objects (unified for both Light and Shape)
	std::vector<PlacedObjectInfo> PlacedObjects;
	int32 SelectedPlacedObjectIndex = -1;

	// Gizmo settings
	ImGuizmo::OPERATION GizmoOperation = ImGuizmo::TRANSLATE;
	ImGuizmo::MODE GizmoMode = ImGuizmo::WORLD;
	bool UseSnap = false;
	float TranslationSnap[3] = {1.0f, 1.0f, 1.0f};
	float RotationSnap = 15.0f;  // degrees
	float ScaleSnap[3] = {0.1f, 0.1f, 0.1f};

	// Tab Selection
	EPlacementTab CurrentTab = EPlacementTab::LIGHT;

	// Type Selection
	EPlacementLightType SelectedLightType = EPlacementLightType::POINT;
	EPlacementShapeType SelectedShapeType = EPlacementShapeType::NONE;

private:
	// Input Handling
	void ProcessPlacementHotkeys(jCamera* mainCamera, float lightColorScale);
	void ProcessTypeSelectionHotkeys();
	void ProcessGizmoHotkeys();

	// Placement
	void PlaceSelectedObject(jCamera* mainCamera, float lightColorScale);
	void PlaceLight(EPlacementLightType lightType, jCamera* mainCamera, float lightColorScale);
	void PlaceShape(EPlacementShapeType shapeType, jCamera* mainCamera);
	void DeletePlacedObject(int32 index);

	// UI Rendering
	void RenderTabBar();
	void RenderLightTab(jCamera* mainCamera, float lightColorScale);
	void RenderShapeTab(jCamera* mainCamera);
	void RenderPlacedObjectsList(jCamera* mainCamera);
	void RenderGizmoControls();
	void RenderSelectedObjectProperties(jCamera* mainCamera);

	// Helper for light creation
	void CreatePointLight(const Vector& position, jCamera* camera, float lightColorScale);
	void CreateSpotLight(const Vector& position, const Vector& direction, jCamera* camera, float lightColorScale);
	void CreateDirectionalLight(const Vector& direction, const Vector& debugPosition, jCamera* camera, float lightColorScale);

	// Helper for shape creation
	void CreateCube(const Vector& position);
	void CreateSphere(const Vector& position);
};

#endif // ENABLE_EDITOR_FEATURES
