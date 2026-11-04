#pragma once
#include "jEditorDocument.h"
#include <vector>
#include <memory>

class jEditor;
struct jRenderFrameContext;
namespace jEditorProcedural
{
// CPU-only meshes/simulation used both by the renderer and regression tests.
struct Vertex
{
    jEditorAuthoring::Vec3 Position{}, Normal{0, 1, 0};
    float U = 0, V = 0;
};
struct Mesh
{
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
};
bool BuildTerrainMesh(const jEditorAuthoring::TerrainProperties& terrain, Mesh& mesh, std::string& error);
struct Particle
{
    jEditorAuthoring::Vec3 Position{}, Velocity{};
    float Age = 0;
};
class ParticleSimulation
{
public:
    void Reset(const jEditorAuthoring::ParticleProperties& settings);
    void Advance(const jEditorAuthoring::ParticleProperties& settings, float deltaTime);
    const std::vector<Particle>& Particles() const { return Live; }
private:
    std::vector<Particle> Live;
    uint32_t RandomState = 1;
    double SpawnRemainder = 0;
};
// Installs owner-thread procedural factories, frame-safe billboard rendering,
// and per-instance COW material application including project-relative textures.
void Install(jEditor& editor);
// Called by renderer Setup after acquiring the frame slot/fence and before any
// render pass. Allocates and fills procedural vertex pages; BindBuffers only binds.
void PrepareRenderResources(const std::shared_ptr<jRenderFrameContext>& frame);
}
