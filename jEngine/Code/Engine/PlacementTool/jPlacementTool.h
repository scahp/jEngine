#pragma once

// Editor-specific features that can be excluded from runtime builds
// Define ENABLE_EDITOR_FEATURES to include editor functionality

#ifdef ENABLE_EDITOR_FEATURES

#include "Code/External/IMGUI/ImGuizmo/ImGuizmo.h"
#include "imgui.h"

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
	EPlacedObjectType Type = EPlacedObjectType::SHAPE;
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
	int32 SelectedRenderObjectIndex = -1;  // Index of selected RenderObject within selected jObject (deprecated for multi-selection)
	std::vector<int32> SelectedRenderObjectIndices;  // Multiple selected RenderObject indices (for Shift+Click)

	// Object Picking via mouse click
	jObject* PickedObject = nullptr;
	void SelectObject(jObject* obj);
	void SelectObject(jRenderObject* renderObj);

	// Editor/main thread only, just like scene registration and RenderUI. The
	// returned list is invalidated by a filter or scene-registration change.
	void SelectPlacedObject(int32 index);
	void SetSceneFilter(const char* filter);
	const std::vector<int32>& GetSceneListIndices();
	// Single mesh/primitives: UI/gizmo angles are degrees; runtime setters use radians.
	// Returns false without changing the scene for invalid values or a group selection.
	bool ApplySelectedTransform(const Vector& position, const Vector& rotationDegrees, const Vector& scale);

	// Register a static object (called from jObject::AddObject)
	void RegisterStaticObject(jObject* obj);
	void UnregisterStaticObject(jObject* obj);
	void RegisterLight(jLight* light);
	void UnregisterLight(jLight* light);

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
	EPlacementShapeType SelectedShapeType = EPlacementShapeType::CUBE;

private:
	bool IsKeyPressed(EInputKey key) const;
	void MoveSceneSelection(int32 direction);
	void RebuildSceneList();
	ImGuiTextFilter SceneFilter;
	std::vector<int32> SceneListIndices;
	std::vector<std::string> SceneObjectLabels;
	bool SceneListDirty = true;
	bool RevealSceneSelection = false;
	bool SceneListHasFocus = false;
	std::array<bool, static_cast<size_t>(EInputKey::MAX)> PreviousKeys{};
	std::array<bool, static_cast<size_t>(EInputKey::MAX)> PressedKeys{};

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
