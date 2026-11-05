#pragma once

#ifdef ENABLE_EDITOR_FEATURES

#include "PlacementTool/jPlacementTool.h"
#include "Editor/jEditorDocument.h"
#include <functional>
#include <memory>

class jEditorRuntime;

// Authoring commands and world publication run on the creating/scene owner thread.
// Document edits are cheap CPU data edits. Tick publishes them before rendering;
// workspace drawing should never call GPU publication itself.
class jEditor
{
public:
    using EntityId = jEditorAuthoring::EntityId;
    using EntityKind = jEditorAuthoring::EntityKind;
    using AssetResolver = std::function<std::filesystem::path(const std::string& assetId, const std::string& source)>;
    using ProceduralFactory = std::function<std::shared_ptr<jObject>(const jEditorAuthoring::Entity&, std::string& error)>;
    using ProceduralUpdater = std::function<void(const jEditorAuthoring::Entity&, jObject&, float deltaTime, float playTime)>;
    using MaterialApplier = std::function<void(const jEditorAuthoring::MaterialProperties&, jObject&)>;

    struct PlacementBounds { jEditorAuthoring::Vec3 Min, Max; };
    struct PlacementHit
    {
        jEditorAuthoring::Vec3 Point, Normal{0, 1, 0};
        EntityId Surface = 0;
    };
    enum class PlacementReadiness { Ready, Pending, Failed };
    enum class WorldAxis { X, Y, Z };

    jEditor();
    ~jEditor();
    jEditor(const jEditor&) = delete;
    jEditor& operator=(const jEditor&) = delete;

    jEditorAuthoring::Document& GetDocument();
    const jEditorAuthoring::Document& GetDocument() const;
    void SetProjectRoot(const std::filesystem::path& root);
    const std::filesystem::path& GetProjectRoot() const;
    void SetAssetResolver(AssetResolver resolver);
    void SetProceduralFactory(ProceduralFactory factory, ProceduralUpdater update = {});
    void SetMaterialApplier(MaterialApplier apply);
    jEditorAuthoring::MaterialProperties ResolveMaterial(const jEditorAuthoring::MaterialProperties& material);

    // Call after existing import publication; paths are relative to project root.
    void TrackWorldObjectSource(jObject* object, const std::string& source,
        const std::string& assetId = {}, uint64_t revision = 0);
    bool CaptureWorldToDocument();
    bool SyncDocumentToWorld();
    void Tick(float deltaTime, jCamera* camera);
    // Before an external scene owner destroys/replaces its objects. Owned editor
    // instances are unregistered; borrowed originals remain with their old owner.
    void ResetWorldBindings(bool newDocument = true);
    void ShutdownWorld(); // Idempotent; call before Game/RHI teardown.

    void SetAuthoringEnabled(bool enabled);
    bool IsAuthoringEnabled() const;
    void ProcessInput(float deltaTime, jCamera* camera);
    void SetViewportInputState(bool hovered, bool focused);
    void SetShortcutInputState(bool editorWindowFocused);
    bool HandleCameraInput(float deltaTime, jCamera* camera);
    bool HandleCameraMouseMove(int32 xOffset, int32 yOffset, jCamera* camera);
    bool RenderViewportGizmo(jCamera* camera, float x = 0, float y = 0, float width = 0, float height = 0);
    bool CancelViewportDuplicateDrag();

    EntityId GetSelectedEntityId() const;
    const std::vector<EntityId>& GetSelectedEntities() const;
    bool SelectEntity(EntityId id, bool additive = false);
    void ToggleEntitySelection(EntityId id);
    void SetSelectedEntities(const std::vector<EntityId>& ids);
    EntityId EntityForObject(const jObject* object) const;
    jObject* ObjectForEntity(EntityId id) const;
    std::vector<jRenderObject*> GetSelectionRenderObjects() const;
    jLight* LightForEntity(EntityId id) const;
    EntityId CreateEntity(EntityKind kind, const std::string& name = {}, EntityId parent = 0);
    EntityId AddModel(const std::string& source, const std::string& assetId = {}, uint64_t revision = 0,
        std::optional<jEditorAuthoring::Vec3> position = std::nullopt);
    PlacementReadiness ReplaceModelAssets(std::span<const EntityId> ids, const std::string& source,
        const std::string& assetId, uint64_t revision);
    std::optional<PlacementHit> FindPlacementHit(const jEditorAuthoring::Vec3& origin,
        const jEditorAuthoring::Vec3& direction, float range, bool surfaceSnap);
    PlacementReadiness ModelPlacementBounds(const std::string& source, const std::string& assetId,
        uint64_t revision, PlacementBounds& bounds, bool prepare);
    PlacementReadiness PrefabPlacementBounds(const jEditorAuthoring::Document& prefab,
        PlacementBounds& bounds, bool prepare);
    void ReleasePlacementModels();
    bool PlaceSelectionOnGround();
    EntityId DuplicateSelected();
    bool DuplicateAndMove(const jEditorAuthoring::Vec3& worldOffset);
    bool CanRepeatDuplicateOffset() const;
    bool RepeatDuplicateOffset();
    bool AlignSelection(WorldAxis axis);
    bool DistributeSelection(WorldAxis axis);
    bool DeleteSelected();
    bool FocusSelected(jCamera* camera);
    bool BeginPlay();
    bool StopPlay();
    // Preview-only timeline seek. Does not change document history or dirty state.
    bool SetPlayTime(float seconds);
    void ReimportAsset(const std::string& source, uint64_t revision = 0);
    const std::string& LastError() const;
    const std::string& ImportStatus() const;
    size_t PendingImportCount() const;
    void CancelPendingImports();
    float PlayTime() const;

    // Compatibility adapter for existing scene registration and GPU picking.
    jPlacementTool Placement;

private:
    std::unique_ptr<jEditorRuntime> Runtime;
};

extern jEditor* g_Editor;

#endif // ENABLE_EDITOR_FEATURES
