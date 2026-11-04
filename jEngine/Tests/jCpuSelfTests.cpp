#include "pch.h"
#include "Math/Quaternion.h"
#include "Renderer/jSurfelGILimits.h"

int RunCoreAuditTests();
int RunLogSelfTests();
int RunAssetImportTests();
int RunEditorWorkflowSelfTests();
int RunEditorAssetSelfTests();
int RunIoAuditTests();
int RunEffectsAuditTests();
int RunRuntimeShaderAuditTests();
int RunGraphicsAuditTests();
int RunEditorDocumentTests();
int RunEditorProjectTests();
int RunEditorGraphicsTests();
int RunEditorAuthoringRuntimeTests();
int RunEditorToolsTests();
int RunModelUploadCpuTests();
int RunEditorExtensionTests();
int RunEditorProjectSettingsTests();
int RunModelImportSettingsTests();

int RunCpuSelfTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
    };
    const auto nearlyEqual = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
    const Vector a(1, 2, 3), b(4, 5, 6);
    expect(nearlyEqual(Vector::DotProduct(a, b), 32), "vector dot product");
    const auto cross = Vector::CrossProduct(a, b);
    expect(nearlyEqual(cross.x, -3) && nearlyEqual(cross.y, 6) && nearlyEqual(cross.z, -3), "vector cross product");
    const auto normalized = Vector(3, 4, 0).GetNormalize();
    expect(nearlyEqual(normalized.x, 0.6f) && nearlyEqual(normalized.y, 0.8f), "vector normalization");
    const auto transform = Matrix::MakeTranslate(10, 20, 30) * Matrix::MakeScale(2, 3, 4);
    const auto point = transform.TransformPoint(a);
    expect(nearlyEqual(point.x, 12) && nearlyEqual(point.y, 26) && nearlyEqual(point.z, 42), "matrix composition and point transform");
    const auto restored = transform.GetInverse().TransformPoint(point);
    expect(nearlyEqual(restored.x, a.x) && nearlyEqual(restored.y, a.y) && nearlyEqual(restored.z, a.z), "matrix inverse roundtrip");
    const auto direction = Quaternion(0, 0, 0, 1).TransformDirection(a);
    expect(nearlyEqual(direction.x, a.x) && nearlyEqual(direction.y, a.y) && nearlyEqual(direction.z, a.z), "identity quaternion direction");
    expect(Align<uint64>(257, 256) == 512 && Align<uint64>(256, 256) == 256, "buffer alignment boundaries");
    expect(jSurfelGILimits::CanAllocate(241664, 160 * 90), "default surfel clipmap budget");
    expect(!jSurfelGILimits::CanAllocate(int64_t(3) * 512 * 512 * 512, 160 * 90), "oversized surfel clipmap rejected before allocation");
    expect(jSurfelGILimits::CanAllocate(jSurfelGILimits::MaxSurfels, jSurfelGILimits::MaxLinearDispatchThreads)
        && !jSurfelGILimits::CanAllocate(jSurfelGILimits::MaxSurfels, jSurfelGILimits::MaxLinearDispatchThreads + 1), "surfel dispatch boundary");
    failures += RunCoreAuditTests();
    failures += RunLogSelfTests();
    failures += RunAssetImportTests();
    failures += RunEditorWorkflowSelfTests();
    failures += RunEditorAssetSelfTests();
    failures += RunIoAuditTests();
    failures += RunEffectsAuditTests();
    failures += RunGraphicsAuditTests();
    failures += RunRuntimeShaderAuditTests();
    failures += RunEditorDocumentTests();
    failures += RunEditorProjectTests();
    failures += RunEditorGraphicsTests();
    failures += RunEditorAuthoringRuntimeTests();
    failures += RunEditorToolsTests();
    failures += RunModelUploadCpuTests();
    failures += RunEditorExtensionTests();
    failures += RunEditorProjectSettingsTests();
    failures += RunModelImportSettingsTests();
    std::cout << "CPU checks: " << (failures ? "FAILED" : "PASSED") << std::endl;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
