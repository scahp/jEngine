#include "pch.h"
#include "Math/Quaternion.h"

int RunCpuSelfTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
    };
    const auto near = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
    const Vector a(1, 2, 3), b(4, 5, 6);
    expect(near(Vector::DotProduct(a, b), 32), "vector dot product");
    const auto cross = Vector::CrossProduct(a, b);
    expect(near(cross.x, -3) && near(cross.y, 6) && near(cross.z, -3), "vector cross product");
    const auto normalized = Vector(3, 4, 0).GetNormalize();
    expect(near(normalized.x, 0.6f) && near(normalized.y, 0.8f), "vector normalization");
    const auto transform = Matrix::MakeTranslate(10, 20, 30) * Matrix::MakeScale(2, 3, 4);
    const auto point = transform.TransformPoint(a);
    expect(near(point.x, 12) && near(point.y, 26) && near(point.z, 42), "matrix composition and point transform");
    const auto restored = transform.GetInverse().TransformPoint(point);
    expect(near(restored.x, a.x) && near(restored.y, a.y) && near(restored.z, a.z), "matrix inverse roundtrip");
    const auto direction = Quaternion(0, 0, 0, 1).TransformDirection(a);
    expect(near(direction.x, a.x) && near(direction.y, a.y) && near(direction.z, a.z), "identity quaternion direction");
    expect(Align<uint64>(257, 256) == 512 && Align<uint64>(256, 256) == 256, "buffer alignment boundaries");
    std::cout << "CPU checks: " << (failures ? "FAILED" : "PASSED") << std::endl;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
