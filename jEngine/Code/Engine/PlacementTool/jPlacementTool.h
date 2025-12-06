#pragma once

// Editor-specific features that can be excluded from runtime builds
// Define ENABLE_EDITOR_FEATURES to include editor functionality

#ifdef ENABLE_EDITOR_FEATURES

#include "Code/External/IMGUI/ImGuizmo/ImGuizmo.h"

class jLight;
class jObject;
class jRenderObject;
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
	int32 SelectedRenderObjectIndex = -1;  // Index of selected RenderObject within selected jObject

	// Object Picking via mouse click
	jObject* PickedObject = nullptr;
	void SelectObject(jObject* obj);
	void SelectObject(jRenderObject* renderObj);

	// Register a static object (called from jObject::AddObject)
	void RegisterStaticObject(jObject* obj);
	void UnregisterStaticObject(jObject* obj);

	// Pick request state (set by mouse click, consumed by renderer)
	bool bPickRequested = false;
	int32 PickMouseX = 0;
	int32 PickMouseY = 0;

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
	void RenderPlacedRenderObjectsList();
	void RenderGizmoControls();
	void RenderSelectedObjectProperties(jCamera* mainCamera);
	void RenderSelectedRenderObjectProperties();

	// Helper for light creation
	void CreatePointLight(const Vector& position, jCamera* camera, float lightColorScale);
	void CreateSpotLight(const Vector& position, const Vector& direction, jCamera* camera, float lightColorScale);
	void CreateDirectionalLight(const Vector& direction, const Vector& debugPosition, jCamera* camera, float lightColorScale);

	// Helper for shape creation
	void CreateCube(const Vector& position);
	void CreateSphere(const Vector& position);
};

#endif // ENABLE_EDITOR_FEATURES
