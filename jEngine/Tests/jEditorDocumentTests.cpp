#include "pch.h"
#include "Code/Engine/Editor/jEditorDocument.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

int RunEditorDocumentTests()
{
    using namespace jEditorAuthoring;
    using Json = nlohmann::json;
    int failures = 0, checks = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << "editor document: " << name << '\n';
        ++checks; failures += !condition;
    };
    const auto fixture = std::filesystem::temp_directory_path() / ("jengine-document-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixture);
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream output(path, std::ios::binary); output << text;
    };
    const auto nearly = [](float a, float b) { return std::abs(a - b) < 0.001f * std::max(1.0f, std::abs(a)); };
    try
    {
        Document scene;
        expect(!scene.IsDirty() && scene.Entities().empty() && !scene.CanUndo(), "new document starts clean and empty");
        expect(scene.NewScene("Authoring regression / 장면"), "UTF-8 scene name accepted");
        std::vector<EntityId> ids;
        bool mixed = true;
        for (int i = 0; i < 100; ++i)
        {
            const auto id = scene.Create(EntityKind(i % 10), "Entity " + std::to_string(i), i < 10 ? 0 : ids[i % 10]);
            if (!id) { mixed = false; break; }
            ids.push_back(id);
            Entity entity = *scene.Find(id);
            entity.Visible = i % 7 != 0;
            entity.Local.Position = {float(i), float(i % 5), float(-i)};
            entity.Local.RotationDegrees = {float(i % 30), float(i % 60), float(i % 45)};
            entity.Local.Scale = {1 + float(i % 4) * 0.1f, 1, 1};
            entity.Material.BaseColor = {float(i % 10) / 10, 0.4f, 0.6f};
            entity.Material.Metallic = float(i % 10) / 10;
            entity.Material.Roughness = float(i % 5) / 5;
            entity.Material.GraphAsset = "Assets/Materials/test.jmaterial";
            entity.Light.Intensity = float(i + 1);
            entity.Camera.Primary = i == 7;
            entity.Particles.SpawnRate = float(i + 1);
            entity.Navigation.Enabled = i % 5 == 0;
            if (entity.Kind == EntityKind::Model)
            {
                entity.SourceAsset = "Assets/Models/sample.obj";
                entity.MeshLocalTransforms = {Transform{}, Transform{{5, 2, 3}, {15, 30, 45}, {1, 2, 1}}};
                entity.AssetId = "e99db57b-79b4-4f03-868d-b18ea651eafe";
                entity.Material.BaseColorTexture = "Assets/Textures/color.png";
            }
            if (entity.Kind == EntityKind::Terrain)
            {
                entity.Terrain.Resolution = 3;
                entity.Terrain.Heights = {0, 1, 2, 1, 2, 3, 2, 3, 4};
            }
            if (i % 11 == 0)
            {
                entity.Animation.Enabled = true;
                Transform end = entity.Local; end.Position.X += 20;
                entity.Animation.Keys = {{0, entity.Local}, {1, end}};
            }
            mixed &= scene.Update(entity);
        }
        expect(mixed && ids.size() == 100 && scene.Entities().size() == 100, "100 mixed authored entities with hierarchy and properties");
        const auto scenePath = fixture / std::filesystem::path(u8"테스트.jscene");
        expect(scene.Save(scenePath) && !scene.IsDirty() && scene.Path() == scenePath, "atomic Unicode scene save marks exact content clean");
        const std::string saved = read(scenePath);
        Document reopened;
        expect(reopened.Load(scenePath) && reopened.ToJson() == saved && reopened.Entities() == scene.Entities()
            && reopened.Name() == scene.Name(), "100 entities reopen with identical IDs, hierarchy, components and typed properties");
        Entity changed = *scene.Find(ids.back()); changed.Name = "Changed";
        const uint64_t beforeRevision = scene.Revision();
        expect(scene.Update(changed) && scene.IsDirty() && scene.Revision() > beforeRevision, "property edit advances monotonic revision and dirty state");
        expect(scene.Undo() && !scene.IsDirty() && scene.ToJson() == saved && scene.Revision() > beforeRevision,
            "Undo returns to saved content token while revision still increases");
        expect(scene.Redo() && scene.IsDirty() && scene.Find(ids.back())->Name == "Changed", "Redo restores edited property");
        auto backupPath = scenePath; backupPath += ".bak";
        expect(scene.Save(scenePath) && read(backupPath) == saved,
            "single backup contains the previous complete saved scene");
        const auto oldSaved = read(scenePath);
        changed = *scene.Find(ids.back()); changed.Name = "Not saved";
        scene.Update(changed);
        expect(!scene.Save(fixture / "missing-parent" / "scene.jscene") && scene.IsDirty() && read(scenePath) == oldSaved,
            "failed save preserves current scene, dirty state and existing file");

        Document commands;
        const EntityId root = commands.Create(EntityKind::Empty, "Root");
        const EntityId child = commands.Create(EntityKind::Cube, "Child", root);
        const EntityId grandchild = commands.Create(EntityKind::PointLight, "Grandchild", child);
        Entity value = *commands.Find(root); value.Local.Position = {10, 20, 30}; value.Local.RotationDegrees = {20, 35, 15}; value.Local.Scale = {2, 2, 2}; commands.Update(value);
        value = *commands.Find(child); value.Local.Position = {5, -4, 2}; value.Local.RotationDegrees = {10, 15, 25}; commands.Update(value);
        float before[16]{}, after[16]{};
        commands.GetWorldMatrix(child, before);
        expect(commands.Reparent(child, 0) && commands.Find(child)->Parent == 0 && commands.GetWorldMatrix(child, after), "reparent computes a world-preserving local transform");
        bool sameWorld = true;
        for (int i = 0; i < 16; ++i) sameWorld &= nearly(before[i], after[i]);
        expect(sameWorld, "world translation, rotation and scale preserved by reparent");
        expect(commands.Undo() && commands.Find(child)->Parent == root && commands.Redo() && commands.Find(child)->Parent == 0,
            "hierarchy command Undo/Redo restores parent and local transform");
        expect(commands.Reparent(child, root), "reparent back under original root");
        const auto stable = commands.ToJson();
        expect(!commands.Reparent(root, grandchild) && commands.ToJson() == stable, "ancestor cycle edit rejected transactionally");
        expect(!commands.Reparent(child, child) && !commands.Reparent(child, 999999) && commands.ToJson() == stable,
            "self-parent and dangling-parent edits rejected");
        const EntityId duplicate = commands.Duplicate(child);
        expect(duplicate && commands.Find(duplicate)->Parent == root && commands.Children(duplicate).size() == 1
            && commands.Find(commands.Children(duplicate)[0])->Kind == EntityKind::PointLight, "duplicate recursively remaps stable IDs and keeps properties");
        const EntityId duplicateChild = commands.Children(duplicate).front();
        commands.Select(duplicateChild);
        expect(commands.Delete(duplicate) && !commands.Find(duplicate) && !commands.Find(duplicateChild) && commands.SelectedId() == 0,
            "delete removes complete subtree and clears invalid selection");
        expect(commands.Undo() && commands.Find(duplicateChild) && commands.SelectedId() == duplicateChild,
            "Undo subtree deletion restores valid selected ID");
        expect(commands.Redo() && !commands.Find(duplicateChild) && commands.SelectedId() == 0,
            "Redo subtree deletion clears selection without stale pointer");
        expect(commands.Undo() && commands.Undo() && !commands.Find(duplicate), "Undo duplicate removes cloned subtree atomically");
        const EntityId fresh = commands.Create(EntityKind::Sphere, "Fresh");
        expect(fresh > duplicateChild && !commands.CanRedo(), "branching after Undo never reuses allocated IDs and discards redo history");

        commands.Select(child);
        const Transform original = commands.Find(child)->Local;
        expect(commands.BeginTransaction("Move drag") && !commands.BeginTransaction("Nested"), "one coalesced edit transaction and no nested transaction");
        bool dragged = true;
        for (int i = 0; i < 50; ++i)
        {
            Entity edit = *commands.Find(child); edit.Local.Position.X = original.Position.X + float(i + 1);
            dragged &= commands.Update(edit, "Move");
        }
        expect(dragged && !commands.CanUndo() && commands.CommitTransaction() && commands.UndoLabel() == "Move drag",
            "50 real transform updates become one labeled Undo command");
        expect(commands.Undo() && commands.Find(child)->Local == original && commands.Redo()
            && nearly(commands.Find(child)->Local.Position.X, original.Position.X + 50), "coalesced drag Undo/Redo restores complete endpoints");
        const auto beforeCancel = commands.ToJson();
        commands.BeginTransaction("Cancel drag");
        value = *commands.Find(child); value.Local.Position.Y += 70; commands.Update(value);
        expect(commands.CancelTransaction() && commands.ToJson() == beforeCancel, "cancel drag restores complete scene without history item");
        commands.BeginTransaction("Noop");
        value = *commands.Find(child); value.Local.Position.Z += 1; commands.Update(value);
        value.Local.Position.Z -= 1; commands.Update(value);
        const auto undoBeforeNoop = commands.UndoLabel();
        expect(commands.CommitTransaction() && commands.UndoLabel() == "Move drag", "net-zero transaction creates no Undo item");
        (void)undoBeforeNoop;

        value = *commands.Find(child); value.Locked = true;
        expect(commands.Update(value) && commands.IsEffectivelyLocked(grandchild), "parent lock inherited by descendants");
        const auto lockedState = commands.ToJson();
        Entity lockedEdit = *commands.Find(grandchild); lockedEdit.Name = "Forbidden";
        expect(!commands.Update(lockedEdit) && !commands.Delete(root) && !commands.Duplicate(root) && commands.ToJson() == lockedState,
            "locked descendant blocks edits, destructive ancestor deletion and duplication");
        value = *commands.Find(child); value.Locked = false;
        expect(commands.Update(value) && !commands.IsEffectivelyLocked(grandchild), "explicit unlock allowed without bypassing ancestor locks");
        value = *commands.Find(root); value.Visible = false; commands.Update(value);
        expect(!commands.IsEffectivelyVisible(grandchild), "visibility inherited through hierarchy");

        Document shear;
        const auto scaleParent = shear.Create(EntityKind::Empty, "Nonuniform");
        const auto rotated = shear.Create(EntityKind::Cube, "Rotated", scaleParent);
        value = *shear.Find(scaleParent); value.Local.Scale = {2, 1, 1}; shear.Update(value);
        value = *shear.Find(rotated); value.Local.RotationDegrees.Z = 45; shear.Update(value);
        const auto shearBefore = shear.ToJson();
        Transform world;
        expect(shear.GetWorldMatrix(rotated, before) && !shear.GetWorldTransform(rotated, world)
            && !shear.Reparent(rotated, 0) && shear.ToJson() == shearBefore,
            "nonrepresentable shear remains intact and world-preserving reparent fails atomically");

        const auto prefab = fixture / "assembly.jprefab";
        expect(commands.SavePrefab(child, prefab), "prefab saves a complete detached subtree");
        const auto count = commands.Entities().size();
        const auto instance = commands.InstantiatePrefab(prefab, root);
        expect(instance && commands.Entities().size() == count + 2 && commands.Children(instance).size() == 1
            && commands.Find(instance)->Parent == root, "prefab instantiate creates remapped hierarchy under selected parent");
        expect(commands.Undo() && commands.Entities().size() == count && !commands.Find(instance) && commands.Redo() && commands.Find(instance),
            "prefab instantiation is a single reversible command");
        commands.Select(grandchild);
        const auto beforePlay = commands.ToJson();
        const auto oldDirty = commands.IsDirty();
        const auto oldUndo = commands.UndoLabel();
        expect(commands.BeginPlay() && commands.IsPlaying() && !commands.BeginPlay(), "Play snapshots scene and guards nested Play");
        commands.Create(EntityKind::Cube, "Play-only entity");
        commands.Delete(root);
        commands.BeginTransaction("Unfinished runtime edit");
        expect(!commands.Save(scenePath) && !commands.NewScene() && !commands.Load(scenePath), "Play blocks persistent save/load/new-scene operations");
        expect(commands.StopPlay() && !commands.IsPlaying() && !commands.HasTransaction() && commands.ToJson() == beforePlay
            && commands.IsDirty() == oldDirty && commands.SelectedId() == grandchild && commands.UndoLabel() == oldUndo,
            "Stop restores scene, selection, dirty token and pre-Play Undo history");

        Document animation;
        const auto animated = animation.Create(EntityKind::Cube, "Animated");
        value = *animation.Find(animated);
        value.Animation.Enabled = true; value.Animation.Duration = 2;
        Transform start, finish; start.RotationDegrees.Y = 350; finish.Position.X = 100; finish.RotationDegrees.Y = 10;
        value.Animation.Keys = {{0, start}, {2, finish}};
        expect(animation.Update(value), "typed animation keyframes accepted");
        const auto animationRevision = animation.Revision();
        expect(animation.EvaluateLocal(animated, 1, world) && nearly(world.Position.X, 50) && nearly(world.RotationDegrees.Y, 360),
            "animation linearly samples position and shortest degree rotation path");
        expect(animation.EvaluateLocal(animated, 3, world) && nearly(world.Position.X, 50) && animation.Revision() == animationRevision,
            "looping animation sampling does not change revision, dirty state or history");
        value.Animation.Keys[1].Time = 0;
        expect(!animation.Update(value), "duplicate/nonmonotonic animation times rejected");
        value = *animation.Find(animated); value.Animation.Keys.back().Local.Scale.X = -1;
        expect(!animation.Update(value), "animation scale sign change rejected before interpolation crosses singular zero");
        value = *animation.Find(animated); value.Camera.NearPlane = value.Camera.FarPlane;
        expect(!animation.Update(value), "camera far plane must be strictly beyond near plane");
        value = *animation.Find(animated); value.Terrain.Heights = {1, 2};
        expect(!animation.Update(value), "terrain sample count must match bounded resolution");
        value = *animation.Find(animated); value.Material.Roughness = std::numeric_limits<float>::quiet_NaN();
        expect(!animation.Update(value), "NaN properties rejected before serialization/runtime publication");
        value = *animation.Find(animated); value.Local.Scale.X = 0;
        expect(!animation.Update(value), "zero scale rejected for invertible hierarchy transforms");
        value = *animation.Find(animated); value.SourceAsset = "../outside/model.obj";
        expect(!animation.Update(value), "asset path traversal rejected");
        value.SourceAsset = "C:/outside/model.obj";
        expect(!animation.Update(value), "absolute Windows asset references rejected");
        value = *animation.Find(animated); value.Name = std::string("bad\0name", 8);
        expect(!animation.Update(value), "embedded NUL and UI-unsafe text rejected");
        value.Name = std::string("\xc0\xaf", 2);
        expect(!animation.Update(value), "invalid UTF-8 text rejected");

        const auto originalJson = reopened.ToJson();
        const auto originalRevision = reopened.Revision();
        const auto originalPath = reopened.Path();
        const auto rejectJson = [&](Json bad, const char* name) {
            expect(!reopened.LoadJson(bad.dump()) && reopened.ToJson() == originalJson && reopened.Revision() == originalRevision
                && reopened.Path() == originalPath, name);
        };
        Json json = Json::parse(originalJson), bad = json;
        bad["version"] = 2;
        rejectJson(bad, "future scene version rejected without altering current document");
        bad = json; bad["entities"][0]["parent"] = json["entities"][10]["id"];
        bad["entities"][10]["parent"] = json["entities"][0]["id"];
        rejectJson(bad, "file hierarchy cycle rejected transactionally");
        bad = json; bad["entities"][1]["id"] = bad["entities"][0]["id"];
        rejectJson(bad, "duplicate file entity IDs rejected transactionally");
        bad = json; bad["entities"][0]["parent"] = uint64_t(9999999);
        rejectJson(bad, "dangling file parent rejected transactionally");
        bad = json; bad["entities"][0]["id"] = -1;
        rejectJson(bad, "negative file entity ID rejected without unsigned wrap");
        bad = json; bad["entities"][0]["id"] = std::numeric_limits<uint64_t>::max();
        rejectJson(bad, "maximum exhausted file entity ID rejected");
        bad = json; bad["entities"][0]["visible"] = 1;
        rejectJson(bad, "boolean type confusion rejected");
        bad = json; bad["entities"][0]["kind"] = "Unknown";
        rejectJson(bad, "unknown file entity type rejected");
        bad = json; bad["entities"][0]["transform"]["scale"] = {1, 0, 1};
        rejectJson(bad, "invalid serialized transform rejected");
        bad = json; bad["unknown"] = true;
        rejectJson(bad, "unknown scene fields rejected by strict schema");
        bad = json; bad["entities"][0]["material"].erase("roughness");
        rejectJson(bad, "missing nested property rejected");
        expect(!reopened.LoadJson("{\"format\":\"jEngineScene\",\"format\":\"jEngineScene\"}") && reopened.ToJson() == originalJson,
            "duplicate JSON object fields rejected instead of last-value-wins");
        expect(!reopened.LoadJson(std::string(33, '[') + std::string(33, ']')) && reopened.ToJson() == originalJson,
            "deep JSON nesting rejected before materializing unbounded trees");
        write(fixture / "corrupt.jscene", "{not json");
        expect(!reopened.Load(fixture / "corrupt.jscene") && reopened.ToJson() == originalJson && reopened.Path() == originalPath,
            "malformed disk scene leaves previous scene/path/history intact");

        Document copySource;
        const auto copyParent = copySource.Create(EntityKind::Empty, "Source parent");
        const auto copyA = copySource.Create(EntityKind::Model, "Assembly A", copyParent);
        const auto copyB = copySource.Create(EntityKind::Sphere, "Assembly B", copyParent);
        const auto copyChild = copySource.Create(EntityKind::PointLight, "A child", copyA);
        value = *copySource.Find(copyParent); value.Local.Position = {80,20,-40}; value.Local.RotationDegrees.Y = 30; value.Local.Scale = {2,2,2}; copySource.Update(value);
        value = *copySource.Find(copyA); value.SourceAsset = "Assets/assembly.obj"; value.AssetId = "stable-asset";
        value.Local.Position = {5,6,7}; value.Local.RotationDegrees = {10,20,0}; value.MeshLocalTransforms = {Transform{}, Transform{}};
        value.MeshLocalTransforms[1].Position.X = 17; value.Material.OverrideEnabled = true; value.Material.Metallic = .7f;
        value.Material.GraphAsset = "Assets/Materials/assembly.jmaterial";
        value.Animation.Enabled = true; value.Animation.Loop = false;
        auto copyKeyEnd = value.Local; copyKeyEnd.Position.X += 20;
        value.Animation.Keys = {{0,value.Local},{1,copyKeyEnd}}; copySource.Update(value);
        value = *copySource.Find(copyB); value.Local.Position = {-10,5,-2}; value.Local.RotationDegrees = {0,10,30}; copySource.Update(value);
        value = *copySource.Find(copyChild); value.Local.Position = {3,4,5}; copySource.Update(value);
        const auto copyOriginal = copySource.ToJson();
        const auto copyRevision = copySource.Revision();
        const auto clipboard = copySource.SelectionJson({copyChild,copyA,copyB,copyA});
        const auto clipboardJson = Json::parse(clipboard);
        expect(!clipboard.empty() && clipboardJson.at("roots").size() == 2 && clipboardJson.at("entities").size() == 3
            && clipboardJson.at("roots")[0] == copyA && clipboardJson.at("roots")[1] == copyB,
            "multi-copy deduplicates selected descendants and duplicate IDs while preserving separate root order");
        expect(copySource.ToJson() == copyOriginal && copySource.Revision() == copyRevision,
            "copy serializes multiple root subtrees without editing source content or history");
        Document pasteTarget;
        const auto pasteParent = pasteTarget.Create(EntityKind::Empty, "Destination parent");
        value = *pasteTarget.Find(pasteParent); value.Local.Position = {20,30,40}; value.Local.RotationDegrees.Y = -25; value.Local.Scale = {1.5f,1.5f,1.5f}; pasteTarget.Update(value);
        const auto beforePaste = pasteTarget.ToJson();
        const auto pasted = pasteTarget.PasteSelectionJson(clipboard,pasteParent);
        expect(pasted.size() == 2 && pasteTarget.Entities().size() == 4 && pasteTarget.Children(pasted[0]).size() == 1
            && pasteTarget.Find(pasted[0])->Parent == pasteParent && pasteTarget.Find(pasted[1])->Parent == pasteParent,
            "multi-paste remaps a complete forest into actual destination parents atomically");
        bool sameCopiedWorld = pasted.size() == 2;
        if (sameCopiedWorld)
        {
            const EntityId originals[] = {copyA,copyB,copyChild};
            const EntityId copies[] = {pasted[0],pasted[1],pasteTarget.Children(pasted[0]).front()};
            for (int entityIndex = 0; entityIndex < 3; ++entityIndex)
            {
                copySource.GetWorldMatrix(originals[entityIndex],before); pasteTarget.GetWorldMatrix(copies[entityIndex],after);
                for (int component = 0; component < 16; ++component) sameCopiedWorld &= nearly(before[component],after[component]);
            }
        }
        expect(sameCopiedWorld, "copy and paste beneath a transformed parent preserve all root/child world transforms");
        expect(pasted.size() == 2 && pasteTarget.Find(pasted[0])->MeshLocalTransforms == copySource.Find(copyA)->MeshLocalTransforms
            && pasteTarget.Find(pasted[0])->Material == copySource.Find(copyA)->Material && pasteTarget.Find(pasted[0])->AssetId == "stable-asset",
            "multi-paste retains source asset identities, mesh offsets and material override properties");
        const auto afterPaste = pasteTarget.ToJson();
        expect(pasteTarget.UndoLabel() == "Paste selection" && pasteTarget.Undo() && pasteTarget.ToJson() == beforePaste
            && pasteTarget.Redo() && pasteTarget.ToJson() == afterPaste && pasteTarget.SelectedId() == pasted.back(),
            "entire multi-root paste is one Undo/Redo command with a valid primary selected ID");
        const auto pastedAgain = pasteTarget.PasteSelectionJson(clipboard,pasteParent);
        expect(pastedAgain.size() == 2 && pastedAgain.front() > pasted.back() && pasteTarget.Entities().size() == 7,
            "repeated multi-paste allocates fresh destination IDs for every subtree");
        auto badClipboard = clipboardJson; badClipboard["roots"].push_back(badClipboard["roots"][0]);
        const auto beforeBadPaste = pasteTarget.ToJson(); const auto beforeBadRevision = pasteTarget.Revision();
        expect(pasteTarget.PasteSelectionJson(badClipboard.dump()).empty() && pasteTarget.ToJson() == beforeBadPaste
            && pasteTarget.Revision() == beforeBadRevision, "duplicate clipboard roots reject without partially adding entities");
        badClipboard = clipboardJson; badClipboard["roots"] = Json::array({copyA});
        expect(pasteTarget.PasteSelectionJson(badClipboard.dump()).empty() && pasteTarget.ToJson() == beforeBadPaste,
            "clipboard root declaration must cover every complete disconnected subtree");
        badClipboard = clipboardJson; badClipboard["entities"][0]["material"]["opacity"] = 2;
        expect(pasteTarget.PasteSelectionJson(badClipboard.dump()).empty() && pasteTarget.ToJson() == beforeBadPaste,
            "invalid component in one pasted root rejects the whole forest");
        expect(copySource.SelectionJson({}).empty() && copySource.SelectionJson({copyA,999999}).empty()
            && copySource.ToJson() == copyOriginal, "empty or stale copy selection fails without changing source data");
        expect(shear.SelectionJson({rotated}).empty() && shear.ToJson() == shearBefore,
            "copying a root with inherited nonrepresentable shear reports failure without losing its world transform");
        Document pasteShear;
        const auto pasteShearParent = pasteShear.Create(EntityKind::Empty, "Nonuniform destination");
        value = *pasteShear.Find(pasteShearParent); value.Local.Scale = {2,1,1}; pasteShear.Update(value);
        const auto beforeShearPaste = pasteShear.ToJson();
        expect(pasteShear.PasteSelectionJson(clipboard,pasteShearParent).empty() && pasteShear.ToJson() == beforeShearPaste,
            "paste under a parent introducing shear rejects the whole multi-root command");
        value = *pasteTarget.Find(pasteParent); value.Locked = true; pasteTarget.Update(value);
        const auto beforeLockedPaste = pasteTarget.ToJson();
        expect(pasteTarget.PasteSelectionJson(clipboard,pasteParent).empty() && pasteTarget.ToJson() == beforeLockedPaste,
            "locked paste destination cannot receive a partially inserted forest");
        Document pasteCapped(Limits{3,16384,32768,128,256}); pasteCapped.Create(EntityKind::Empty,"Existing");
        const auto cappedBeforePaste = pasteCapped.ToJson();
        expect(pasteCapped.PasteSelectionJson(clipboard).empty() && pasteCapped.ToJson() == cappedBeforePaste,
            "multi-root paste enforces total entity budget before publication");
        Document pasteByteCapped(Limits{100,1024,32768,128,256});
        expect(pasteByteCapped.PasteSelectionJson(clipboard).empty() && pasteByteCapped.Entities().empty(),
            "clipboard input byte limit is enforced before parsing entity trees");
        value = *copySource.Find(copyParent); value.Animation.Enabled = true;
        auto animatedParentEnd = value.Local; animatedParentEnd.Position.Y += 10;
        value.Animation.Keys = {{0,value.Local},{1,animatedParentEnd}}; copySource.Update(value);
        expect(copySource.SelectionJson({copyA,copyB}).empty() && !copySource.SelectionJson({copyParent,copyA,copyB}).empty(),
            "copy requires inherited animated ancestor to preserve root key pose ownership honestly");

        Limits small; small.MaxEntities = 3; small.MaxHistoryEntries = 2; small.MaxFileBytes = 8192;
        Document bounded(small);
        bounded.Create(EntityKind::Cube); bounded.Create(EntityKind::Sphere); bounded.Create(EntityKind::Empty);
        const auto capped = bounded.ToJson();
        expect(!bounded.Create(EntityKind::Cube) && bounded.ToJson() == capped, "entity admission limit rejects edit atomically");
        expect(!bounded.LoadJson(originalJson) && bounded.ToJson() == capped, "scene byte/entity limits checked before replacing scene");
        expect(bounded.Undo() && bounded.Undo() && !bounded.CanUndo() && bounded.Entities().size() == 1,
            "bounded history prunes oldest complete commands");
        const auto boundedBefore = bounded.ToJson();
        bool foreignMutation = true;
        std::thread foreign([&] { foreignMutation = bounded.Create(EntityKind::Camera) != 0; });
        foreign.join();
        expect(!foreignMutation && bounded.ToJson() == boundedBefore, "off-owner-thread mutation rejected without scene changes");
        Document limitedBytes(Limits{100, 16384, 4096, 128, 256});
        const auto limitedEntity = limitedBytes.Create(EntityKind::Cube);
        value = *limitedBytes.Find(limitedEntity); value.Terrain.Resolution = 32; value.Terrain.Heights.assign(1024, 0);
        const auto limitedBefore = limitedBytes.ToJson();
        expect(!limitedBytes.Update(value) && limitedBytes.ToJson() == limitedBefore,
            "single command exceeding undo memory budget rejected before publication");
#ifndef _WIN32
        const auto target = fixture / "symlink.jscene";
        std::filesystem::create_symlink(scenePath, target);
        expect(!reopened.Save(target) && read(scenePath) == oldSaved, "atomic save refuses symlink target without overwriting referenced scene");
#endif
        const auto emptyScenePath = fixture / "empty.jscene";
        write(emptyScenePath, "");
        expect(reopened.Save(emptyScenePath) && read(emptyScenePath) == originalJson,
            "saving over an empty file creates a valid scene and bounded empty backup");
        size_t temporaryCount = 0;
        for (const auto& item : std::filesystem::directory_iterator(fixture))
            temporaryCount += item.path().filename().native().find(std::filesystem::path(".tmp-").native()) != std::filesystem::path::string_type::npos;
        expect(temporaryCount == 0, "successful and failed saves leave no temporary files");
    }
    catch (const std::exception& exception)
    {
        std::cout << "FAIL editor document: unexpected exception: " << exception.what() << '\n';
        ++failures;
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(fixture, cleanupError);
    std::cout << "Editor document tests: " << checks << " checks, " << failures << " failures\n";
    return failures;
}
