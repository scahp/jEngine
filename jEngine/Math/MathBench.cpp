// MathBench: standalone benchmark, does not use pch.h to avoid DirectXMath conflicts
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>
#include "CoreDefines.h"
#include "Generic/TemplateUtility.h"
#include "Math/MathUtility.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"

// Simple micro-benchmark harness for math ops.
// Run as a console app; outputs to stdout and "benchmark_results.txt".

using Clock = std::chrono::high_resolution_clock;

// Volatile scalar sink to prevent the compiler from eliding work.
static volatile float g_sink = 0.0f;

struct BenchResult
{
	const char* Name;
	double Millis;
};

template <typename Fn>
double RunBenchmark(Fn&& fn)
{
	auto const start = Clock::now();
	fn();
	auto const end = Clock::now();
	return std::chrono::duration<double, std::milli>(end - start).count();
}

int main()
{
	std::vector<BenchResult> results;
	results.reserve(32);

	constexpr int kVectorIters = 1'000'000;
	constexpr int kMatrixIters = 250'000;
	constexpr int kQuatIters = 500'000;

	results.push_back({"Vector scale", RunBenchmark([&]()
	{
		Vector v(1.1f, 2.2f, 3.3f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v *= 1.0001f;
		}
		g_sink += v.x + v.y + v.z;
	})});

	results.push_back({"Vector add", RunBenchmark([&]()
	{
		Vector v(1.1f, 2.2f, 3.3f);
		Vector a(0.3f, 0.4f, 0.5f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v += a;
		}
		g_sink += v.x + v.y + v.z;
	})});

	results.push_back({"Vector sub", RunBenchmark([&]()
	{
		Vector v(1.1f, 2.2f, 3.3f);
		Vector a(0.3f, 0.4f, 0.5f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v -= a;
		}
		g_sink += v.x + v.y + v.z;
	})});

	results.push_back({"Vector div", RunBenchmark([&]()
	{
		Vector v(1.1f, 2.2f, 3.3f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v /= 1.0001f;
		}
		g_sink += v.x + v.y + v.z;
	})});

	results.push_back({"Vector dot", RunBenchmark([&]()
	{
		Vector v(1.1f, 2.2f, 3.3f);
		float acc = 0.0f;
		for (int i = 0; i < kVectorIters; ++i)
		{
			acc += Vector::DotProduct(v, v);
			v += Vector(0.0001f, 0.0002f, 0.0003f);
		}
		g_sink += acc + v.x + v.y + v.z;
	})});

	results.push_back({"Vector length", RunBenchmark([&]()
	{
		Vector v(1.1f, 2.2f, 3.3f);
		float acc = 0.0f;
		for (int i = 0; i < kVectorIters; ++i)
		{
			acc += v.Length();
			v += Vector(0.0001f, 0.0002f, 0.0003f);
		}
		g_sink += acc + v.x + v.y + v.z;
	})});

	results.push_back({"Vector normalize", RunBenchmark([&]()
	{
		Vector v(1.1f, 2.2f, 3.3f);
		float acc = 0.0f;
		for (int i = 0; i < kVectorIters; ++i)
		{
			v.SetNormalize();
			acc += v.x + v.y + v.z;
			v += Vector(0.0001f, 0.0002f, 0.0003f);
		}
		g_sink += acc + v.x + v.y + v.z;
	})});

	results.push_back({"Vector cross product", RunBenchmark([&]()
	{
		Vector v0(1.0f, 0.0f, 0.0f);
		Vector v1(0.0f, 1.0f, 0.0f);
		Vector result{ZeroType};
		for (int i = 0; i < kVectorIters; ++i)
		{
			result += Vector::CrossProduct(v0, v1);
			v0 += Vector(0.0001f, 0.0002f, 0.0003f);
			v1 += Vector(-0.0003f, 0.0002f, -0.0001f);
		}
		g_sink += result.x + result.y + result.z;
	})});

	results.push_back({"Vector4 scale", RunBenchmark([&]()
	{
		Vector4 v(1.1f, 2.2f, 3.3f, 4.4f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v *= 1.0001f;
		}
		g_sink += v.x + v.y + v.z + v.w;
	})});

	results.push_back({"Vector4 add", RunBenchmark([&]()
	{
		Vector4 v(1.1f, 2.2f, 3.3f, 4.4f);
		Vector4 a(0.3f, 0.4f, 0.5f, 0.6f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v += a;
		}
		g_sink += v.x + v.y + v.z + v.w;
	})});

	results.push_back({"Vector4 sub", RunBenchmark([&]()
	{
		Vector4 v(1.1f, 2.2f, 3.3f, 4.4f);
		Vector4 a(0.3f, 0.4f, 0.5f, 0.6f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v -= a;
		}
		g_sink += v.x + v.y + v.z + v.w;
	})});

	results.push_back({"Vector4 div", RunBenchmark([&]()
	{
		Vector4 v(1.1f, 2.2f, 3.3f, 4.4f);
		for (int i = 0; i < kVectorIters; ++i)
		{
			v /= 1.0001f;
		}
		g_sink += v.x + v.y + v.z + v.w;
	})});

	results.push_back({"Vector4 dot", RunBenchmark([&]()
	{
		Vector4 v(1.1f, 2.2f, 3.3f, 4.4f);
		Vector4 a(0.3f, 0.4f, 0.5f, 0.6f);
		float acc = 0.0f;
		for (int i = 0; i < kVectorIters; ++i)
		{
			acc += Vector4::DotProduct(v, a);
			v += Vector4(0.0001f, 0.0002f, 0.0003f, 0.0004f);
		}
		g_sink += acc + v.x + v.y + v.z + v.w;
	})});

	results.push_back({"Vector4 length", RunBenchmark([&]()
	{
		Vector4 v(1.1f, 2.2f, 3.3f, 4.4f);
		float acc = 0.0f;
		for (int i = 0; i < kVectorIters; ++i)
		{
			acc += v.Length();
			v += Vector4(0.0001f, 0.0002f, 0.0003f, 0.0004f);
		}
		g_sink += acc + v.x + v.y + v.z + v.w;
	})});

	results.push_back({"Vector4 normalize", RunBenchmark([&]()
	{
		Vector4 v(1.1f, 2.2f, 3.3f, 4.4f);
		float acc = 0.0f;
		for (int i = 0; i < kVectorIters; ++i)
		{
			v.SetNormalize();
			acc += v.x + v.y + v.z + v.w;
			v += Vector4(0.0001f, 0.0002f, 0.0003f, 0.0004f);
		}
		g_sink += acc + v.x + v.y + v.z + v.w;
	})});

	results.push_back({"Matrix4 multiply", RunBenchmark([&]()
	{
		Matrix mA = Matrix::MakeRotate(Vector(0.1f, 0.2f, 0.3f)) * Matrix::MakeTranslate(Vector(1.0f, 2.0f, 3.0f));
		Matrix mB = Matrix::MakeScale(Vector(1.1f, 0.9f, 1.2f));
		Matrix result{IdentityType};
		for (int i = 0; i < kMatrixIters; ++i)
		{
			result = result * mA;
			result = result * mB;
		}
		float sum = 0.0f;
		for (float f : result.mm) sum += f;
		g_sink += sum;
	})});

	results.push_back({"Matrix4 transform", RunBenchmark([&]()
	{
		Matrix m = Matrix::MakeRotate(Vector(0.1f, 0.2f, 0.3f)) * Matrix::MakeTranslate(Vector(1.0f, 2.0f, 3.0f));
		Vector v(1.0f, 2.0f, 3.0f);
		Vector result{ZeroType};
		for (int i = 0; i < kMatrixIters; ++i)
		{
			result += m.TransformPoint(v);
			v += Vector(0.001f, 0.002f, 0.003f);
		}
		g_sink += result.x + result.y + result.z + m.m00 + m.m11 + m.m22 + m.m33;
	})});

	results.push_back({"Matrix3 multiply", RunBenchmark([&]()
	{
		Matrix3 mA(1.0f, 0.0f, 0.0f,
				   0.0f, 1.0f, 0.0f,
				   0.0f, 0.0f, 1.0f);
		Matrix3 mB(0.9f, 0.1f, 0.0f,
				   -0.1f, 1.1f, 0.0f,
				   0.05f, 0.02f, 1.0f);
		Matrix3 result = mA;
		for (int i = 0; i < kMatrixIters; ++i)
		{
			result = result * mB;
			result = result * mA;
		}
		float sum = result.m00 + result.m01 + result.m02
			+ result.m10 + result.m11 + result.m12
			+ result.m20 + result.m21 + result.m22;
		g_sink += sum;
	})});

	results.push_back({"Matrix3 transform", RunBenchmark([&]()
	{
		Matrix3 m(1.0f, 0.1f, 0.0f,
				  -0.1f, 1.0f, 0.05f,
				  0.02f, -0.03f, 1.0f);
		Vector v(1.0f, 2.0f, 3.0f);
		Vector result{ZeroType};
		for (int i = 0; i < kMatrixIters; ++i)
		{
			result += m.Transform(v);
			v += Vector(0.0005f, -0.0002f, 0.0001f);
		}
		g_sink += result.x + result.y + result.z + m.m00 + m.m11 + m.m22;
	})});

	results.push_back({"Quaternion mul/normalize", RunBenchmark([&]()
	{
		Quaternion q0(0.0f, 0.7071f, 0.0f, 0.7071f);
		Quaternion q1(0.0f, 0.0f, 0.7071f, 0.7071f);
		Quaternion result = q0;
		for (int i = 0; i < kQuatIters; ++i)
		{
			result = result * q1;
			result.SetNormalize();
		}
		g_sink += result.x + result.y + result.z + result.w;
	})});

	results.push_back({"Quaternion transform direction", RunBenchmark([&]()
	{
		Quaternion q;
		q.SetRotation(Vector(0.3f, 0.5f, 0.7f), 1.1f);
		Vector v(1.0f, 0.0f, 0.0f);
		Vector result{ZeroType};
		for (int i = 0; i < kQuatIters; ++i)
		{
			Vector t = q.TransformDirection(v);
			result += t;
			v += Vector(0.0002f, 0.0001f, -0.0001f);
		}
		g_sink += result.x + result.y + result.z + q.x + q.y + q.z + q.w;
	})});

	std::ofstream ofs("benchmark_results.txt", std::ios::out | std::ios::trunc);
	if (ofs)
	{
		ofs << "Math benchmark results (ms)\n";
	}

	for (auto const& r : results)
	{
		std::printf("%-32s : %.3f ms\n", r.Name, r.Millis);
		if (ofs)
		{
			ofs << r.Name << " : " << r.Millis << " ms\n";
		}
	}

	std::printf("\nBenchmarks complete. Press Enter to exit...");
	std::fflush(stdout);
	std::getchar();

	return 0;
}
