#pragma once

/*!
 * \file Vector.h
 * \date 2018/8/4 23:26
 *
 * \author 최재호(scahp)
 * Contact: scahp@naver.com
 *
 * \brief 방향과 점을 정의하는 벡터 클래스, 기본 Vector는 3D (float)
*/

#include <math.h>
#include "CoreDefines.h"
#include "MathUtility.h"
#include "Generic/TemplateUtility.h"

struct Vector4;
struct Vector2;

struct Vector
{
	static const Vector OneVector;
	static const Vector ZeroVector;
	static const Vector FowardVector;
	static const Vector RightVector;
	static const Vector UpVector;

	FORCEINLINE constexpr Vector() { }
	FORCEINLINE constexpr Vector(zero_type /*ZeroType*/) { x = 0.0f; y = 0.0f; z = 0.0f; }
	FORCEINLINE constexpr explicit Vector(float fValue) : x(fValue), y(fValue), z(fValue) { }
	FORCEINLINE constexpr Vector(float fX, float fY, float fZ) : x(fX), y(fY), z(fZ) { }
	Vector(Vector4 const& vector);
	Vector(Vector2 const& vector, float fZ);

	FORCEINLINE Vector operator*(float fValue) const
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 s = _mm_set1_ps(fValue);
		__m128 r = _mm_mul_ps(v, s);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		return Vector(tmp[0], tmp[1], tmp[2]);
#else
		return Vector(x * fValue, y * fValue, z * fValue);
#endif
	}

	/*!
	* \return Vector
	* \param[in] vector	곱해질 벡터
	* \brief 내적과 같은 기능임.
	*/
	FORCEINLINE Vector operator*(Vector const& vector) const
	{
		return Vector(x * vector.x, y * vector.y, z * vector.z);
	}

	FORCEINLINE Vector& operator*=(Vector const& vector)
	{
		x *= vector.x, y *= vector.y, z *= vector.z;
		return *this;
	}

	FORCEINLINE Vector& operator*=(float fValue)
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 s = _mm_set1_ps(fValue);
		__m128 r = _mm_mul_ps(v, s);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		x = tmp[0]; y = tmp[1]; z = tmp[2];
#else
		x *= fValue, y *= fValue, z *= fValue;
#endif
		return *this;
	}

	FORCEINLINE Vector operator+(float fValue) const
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 s = _mm_set1_ps(fValue);
		__m128 r = _mm_add_ps(v, s);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		return Vector(tmp[0], tmp[1], tmp[2]);
#else
		return Vector(x + fValue, y + fValue, z + fValue);
#endif
	}

	FORCEINLINE Vector& operator+=(float fValue)
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 s = _mm_set1_ps(fValue);
		__m128 r = _mm_add_ps(v, s);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		x = tmp[0]; y = tmp[1]; z = tmp[2];
#else
		x += fValue, y += fValue, z += fValue;
#endif
		return *this;
	}
	
	FORCEINLINE Vector operator+(Vector const& vector) const
	{
#if USE_SSE
		__m128 a = _mm_setr_ps(x, y, z, 0.0f);
		__m128 b = _mm_setr_ps(vector.x, vector.y, vector.z, 0.0f);
		__m128 r = _mm_add_ps(a, b);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		return Vector(tmp[0], tmp[1], tmp[2]);
#else
		return Vector(x + vector.x, y + vector.y, z + vector.z);
#endif
	}

	FORCEINLINE Vector& operator+=(Vector const& vector)
	{
#if USE_SSE
		__m128 a = _mm_setr_ps(x, y, z, 0.0f);
		__m128 b = _mm_setr_ps(vector.x, vector.y, vector.z, 0.0f);
		__m128 r = _mm_add_ps(a, b);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		x = tmp[0]; y = tmp[1]; z = tmp[2];
#else
		x += vector.x, y += vector.y, z += vector.z;
#endif
		return *this;
	}

//    friend Vector& operator+=(Vector& lhs, const Vector& rhs)
//    {
//#if USE_SSE
//        __m128 a = _mm_setr_ps(lhs.x, lhs.y, lhs.z, 0.0f);
//        __m128 b = _mm_setr_ps(rhs.x, rhs.y, rhs.z, 0.0f);
//        __m128 r = _mm_add_ps(a, b);
//        float tmp[4];
//        _mm_storeu_ps(tmp, r);
//        lhs.x = tmp[0]; lhs.y = tmp[1]; lhs.z = tmp[2];
//#else
//		lhs.x += rhs.x, lhs.y += rhs.y, lhs.z += rhs.z;
//#endif
//        return lhs;
//    }

	FORCEINLINE Vector operator-(float fValue) const
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 s = _mm_set1_ps(fValue);
		__m128 r = _mm_sub_ps(v, s);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		return Vector(tmp[0], tmp[1], tmp[2]);
#else
		return Vector(x - fValue, y - fValue, z - fValue);
#endif
	}

	FORCEINLINE Vector& operator-=(float fValue)
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 s = _mm_set1_ps(fValue);
		__m128 r = _mm_sub_ps(v, s);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		x = tmp[0]; y = tmp[1]; z = tmp[2];
#else
		x -= fValue, y -= fValue, z -= fValue;
#endif
		return *this;
	}

	FORCEINLINE Vector operator-(Vector const& vector) const
	{
#if USE_SSE
		__m128 a = _mm_setr_ps(x, y, z, 0.0f);
		__m128 b = _mm_setr_ps(vector.x, vector.y, vector.z, 0.0f);
		__m128 r = _mm_sub_ps(a, b);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		return Vector(tmp[0], tmp[1], tmp[2]);
#else
		return Vector(x - vector.x, y - vector.y, z - vector.z);
#endif
	}

	FORCEINLINE Vector& operator-=(Vector const& vector)
	{
#if USE_SSE
		__m128 a = _mm_setr_ps(x, y, z, 0.0f);
		__m128 b = _mm_setr_ps(vector.x, vector.y, vector.z, 0.0f);
		__m128 r = _mm_sub_ps(a, b);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		x = tmp[0]; y = tmp[1]; z = tmp[2];
#else
		x -= vector.x, y -= vector.y, z -= vector.z;
#endif
		return *this;
	}

	FORCEINLINE Vector operator/(float fValue) const
	{
		JASSERT(!::IsNearlyZero(fValue));
		if (::IsNearlyZero(fValue))
			return Vector(ZeroType);

#if USE_SSE
		const float RScale = 1.0f / fValue;
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 s = _mm_set1_ps(RScale);
		__m128 r = _mm_mul_ps(v, s);
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		return Vector(tmp[0], tmp[1], tmp[2]);
#else
		return Vector(x / fValue, y / fValue, z / fValue);
#endif
	}

	FORCEINLINE Vector& operator/=(float fValue)
	{
		JASSERT(!::IsNearlyZero(fValue));
		if (::IsNearlyZero(fValue))
			x = 0.0f, y = 0.0f, z = 0.0f;
		else
		{
#if USE_SSE
			const float RScale = 1.0f / fValue;
			__m128 v = _mm_setr_ps(x, y, z, 0.0f);
			__m128 s = _mm_set1_ps(RScale);
			__m128 r = _mm_mul_ps(v, s);
			float tmp[4];
			_mm_storeu_ps(tmp, r);
			x = tmp[0]; y = tmp[1]; z = tmp[2];
#else
			x /= fValue, y /= fValue, z /= fValue;
#endif
		}
		return *this;
	}

	FORCEINLINE Vector operator-() const
	{
		return Vector(-x, -y, -z);
	}

	FORCEINLINE bool operator==(Vector const& vector) const
	{
		return (x == vector.x) && (y == vector.y) && (z == vector.z);
	}

	FORCEINLINE bool IsNearlyEqual(Vector const& vector, float fTolerance = FLOAT_TOLERANCE) const
	{
		return ::IsNearlyEqual(x, vector.x, fTolerance) && ::IsNearlyEqual(y, vector.y, fTolerance) && ::IsNearlyEqual(z, vector.z, fTolerance);
	}

    FORCEINLINE friend bool IsNearlyEqual(Vector const& A, Vector const& B, float fTolerance = FLOAT_TOLERANCE)
    {
		return A.IsNearlyEqual(B, fTolerance);
    }

	FORCEINLINE friend Vector Abs(Vector A)
	{
		A.x = fabs(A.x);
		A.y = fabs(A.y);
		A.z = fabs(A.z);
		return A;
	}

	FORCEINLINE bool operator!=(Vector const& vector) const
	{
		return !(*this == vector);
	}

	void operator=(struct Vector2 const& vector);
	void operator=(struct Vector4 const& vector);

	FORCEINLINE float DotProduct(Vector const& vector) const
	{
		return Vector::DotProduct(*this, vector);
	}

	FORCEINLINE static float DotProduct(Vector const& A, Vector const& B)
	{
#if USE_SSE
		__m128 a = _mm_setr_ps(A.x, A.y, A.z, 0.0f);
		__m128 b = _mm_setr_ps(B.x, B.y, B.z, 0.0f);
		__m128 mul = _mm_mul_ps(a, b);
		__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
		__m128 sum = _mm_add_ps(mul, shuf);
		shuf = _mm_movehl_ps(shuf, sum);
		sum = _mm_add_ss(sum, shuf);
		return _mm_cvtss_f32(sum);
#else
		auto const& result = A * B;
		return (result.x + result.y + result.z);
#endif
	}

	Vector CrossProduct(Vector const& vector) const
	{
		return Vector::CrossProduct(*this, vector);
	}

	FORCEINLINE static Vector CrossProduct(Vector const& A, Vector const& B)
	{
#if USE_SSE
		__m128 a = _mm_setr_ps(A.x, A.y, A.z, 0.0f);
		__m128 b = _mm_setr_ps(B.x, B.y, B.z, 0.0f);
		__m128 a_yzx = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
		__m128 a_zxy = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2));
		__m128 b_yzx = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
		__m128 b_zxy = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
		__m128 r = _mm_sub_ps(_mm_mul_ps(a_yzx, b_zxy), _mm_mul_ps(a_zxy, b_yzx));
		float tmp[4];
		_mm_storeu_ps(tmp, r);
		return Vector(tmp[0], tmp[1], tmp[2]);
#else
		return Vector(A.y * B.z - B.y * A.z, A.z * B.x - B.z * A.x, A.x * B.y - B.x * A.y);
#endif
	}

	FORCEINLINE float Length() const
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 mul = _mm_mul_ps(v, v);
		__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
		__m128 sum = _mm_add_ps(mul, shuf);
		shuf = _mm_movehl_ps(shuf, sum);
		sum = _mm_add_ss(sum, shuf);
		return sqrtf(_mm_cvtss_f32(sum));
#else
		return sqrtf(x * x + y * y + z * z);
#endif
	}

	FORCEINLINE static float Length(Vector const& A, Vector const& B)
	{
		return (A - B).Length();
	}

	FORCEINLINE float LengthSQ() const
	{
#if USE_SSE
		__m128 v = _mm_setr_ps(x, y, z, 0.0f);
		__m128 mul = _mm_mul_ps(v, v);
		__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
		__m128 sum = _mm_add_ps(mul, shuf);
		shuf = _mm_movehl_ps(shuf, sum);
		sum = _mm_add_ss(sum, shuf);
		return _mm_cvtss_f32(sum);
#else
		return x * x + y * y + z * z;
#endif
	}

	FORCEINLINE static float LengthSQ(Vector const& A, Vector const& B)
	{
		return (A - B).LengthSQ();
	}

	FORCEINLINE bool IsNearlyZero(float fTolerance = FLOAT_TOLERANCE) const
	{
		return ::IsNearlyZero(x, fTolerance) && ::IsNearlyZero(y, fTolerance) && ::IsNearlyZero(z, fTolerance);
	}

    FORCEINLINE friend bool IsNearlyZero(Vector const& vector, float fTolerance = FLOAT_TOLERANCE)
    {
		return vector.IsNearlyZero(fTolerance);
    }

	FORCEINLINE Vector& SetNormalize()
	{
		if (IsNearlyZero())
		{
			JASSERT("Vector length is zero.");
		}
		else
		{
#if USE_SSE
			__m128 v = _mm_setr_ps(x, y, z, 0.0f);
			__m128 mul = _mm_mul_ps(v, v);
			__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
			__m128 sum = _mm_add_ps(mul, shuf);
			shuf = _mm_movehl_ps(shuf, sum);
			sum = _mm_add_ss(sum, shuf);
			__m128 half = _mm_set1_ps(0.5f);
			__m128 three = _mm_set1_ps(1.5f);
			__m128 invLen = _mm_rsqrt_ss(sum);
			// One Newton-Raphson refinement for better accuracy
			invLen = _mm_mul_ss(invLen, _mm_sub_ss(three, _mm_mul_ss(_mm_mul_ss(sum, half), _mm_mul_ss(invLen, invLen))));
			__m128 invLenVec = _mm_shuffle_ps(invLen, invLen, _MM_SHUFFLE(0, 0, 0, 0));
			__m128 result = _mm_mul_ps(v, invLenVec);
			float tmp[4];
			_mm_storeu_ps(tmp, result);
			x = tmp[0]; y = tmp[1]; z = tmp[2];
#else
			float const length = 1.0f / Length();
			x *= length;
			y *= length;
			z *= length;
#endif
		}

		return *this;
	}

	FORCEINLINE Vector GetNormalize() const
	{
		return Vector(*this).SetNormalize();
	}

	Vector GetEulerAngleFrom() const;

	FORCEINLINE static Vector GetEulerAngleFrom(const Vector& direction)
	{
		return direction.GetNormalize().GetEulerAngleFrom();
	}

	Vector GetDirectionFromEulerAngle() const;

	FORCEINLINE static Vector GetDirectionFromEulerAngle(const Vector& eulerAngle)
	{
		return eulerAngle.GetDirectionFromEulerAngle();
	}

	union
	{
		struct { float x, y, z; };
		float v[3];
	};	
};

struct Vector4
{
	static const Vector4 OneVector;
	static const Vector4 ZeroVector;
	static const Vector4 FowardVector;
	static const Vector4 RightVector;
	static const Vector4 UpVector;
	static const Vector4 ColorRed;
	static const Vector4 ColorGreen;
	static const Vector4 ColorBlue;
	static const Vector4 ColorWhite;
	static const Vector4 ColorBlack;

	FORCEINLINE constexpr Vector4() { }
	FORCEINLINE constexpr Vector4(zero_type /*ZeroType*/) { x = 0.0f; y = 0.0f; z = 0.0f; w = 0.0f; }
	FORCEINLINE constexpr explicit Vector4(float fValue) : x(fValue), y(fValue), z(fValue), w(fValue) { }
	FORCEINLINE constexpr Vector4(float fX, float fY, float fZ, float fW) : x(fX), y(fY), z(fZ), w(fW) { }
	Vector4(const Vector& InVector);
	Vector4(const Vector2& InA, const Vector2& InB);
	FORCEINLINE Vector4(Vector vector, float fW) : x(vector.x), y(vector.y), z(vector.z), w(fW) { }

	FORCEINLINE Vector4 operator*(float fValue) const
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 s = _mm_set1_ps(fValue);
		__m128 result = _mm_mul_ps(v, s);
		Vector4 ret;
		_mm_storeu_ps(&ret.x, result);
		return ret;
#else
		return Vector4(x * fValue, y * fValue, z * fValue, w * fValue);
#endif
	}
	
	FORCEINLINE Vector4& operator*=(float fValue)
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 s = _mm_set1_ps(fValue);
		__m128 result = _mm_mul_ps(v, s);
		_mm_storeu_ps(&x, result);
#else
		x *= fValue, y *= fValue, z *= fValue, w *= fValue;
#endif
		return *this;
	}

	/*!
	* \return Vector4
	* \param[in] Vector4	곱해질 벡터
	* \brief 내적과 같은 기능임.
	*/
	FORCEINLINE Vector4 operator*(Vector4 const& vector) const
	{
#if USE_SSE
		__m128 a = _mm_loadu_ps(&x);
		__m128 b = _mm_loadu_ps(&vector.x);
		__m128 result = _mm_mul_ps(a, b);
		Vector4 ret;
		_mm_storeu_ps(&ret.x, result);
		return ret;
#else
		return Vector4(x * vector.x, y * vector.y, z * vector.z, w * vector.w);
#endif
	}

	FORCEINLINE Vector4& operator*=(Vector4 const& vector)
	{
#if USE_SSE
		__m128 a = _mm_loadu_ps(&x);
		__m128 b = _mm_loadu_ps(&vector.x);
		__m128 result = _mm_mul_ps(a, b);
		_mm_storeu_ps(&x, result);
#else
		x *= vector.x, y *= vector.y, z *= vector.z, w *= vector.w;
#endif
		return *this;
	}

	FORCEINLINE Vector4 operator+(float fValue) const
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 s = _mm_set1_ps(fValue);
		__m128 result = _mm_add_ps(v, s);
		Vector4 ret;
		_mm_storeu_ps(&ret.x, result);
		return ret;
#else
		return Vector4(x + fValue, y + fValue, z + fValue, w + fValue);
#endif
	}

	FORCEINLINE Vector4& operator+=(float fValue)
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 s = _mm_set1_ps(fValue);
		__m128 result = _mm_add_ps(v, s);
		_mm_storeu_ps(&x, result);
#else
		x += fValue, y += fValue, z += fValue, w += fValue;
#endif
		return *this;
	}

	FORCEINLINE Vector4 operator+(Vector4 const& vector) const
	{
#if USE_SSE
		__m128 a = _mm_loadu_ps(&x);
		__m128 b = _mm_loadu_ps(&vector.x);
		__m128 result = _mm_add_ps(a, b);
		Vector4 ret;
		_mm_storeu_ps(&ret.x, result);
		return ret;
#else
		return Vector4(x + vector.x, y + vector.y, z + vector.z, w + vector.w);
#endif
	}

	FORCEINLINE Vector4& operator+=(Vector4 const& vector)
	{
#if USE_SSE
		__m128 a = _mm_loadu_ps(&x);
		__m128 b = _mm_loadu_ps(&vector.x);
		__m128 result = _mm_add_ps(a, b);
		_mm_storeu_ps(&x, result);
#else
		x += vector.x, y += vector.y, z += vector.z, w += vector.w;
#endif
		return *this;
	}

	FORCEINLINE Vector4 operator-(float fValue) const
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 s = _mm_set1_ps(fValue);
		__m128 result = _mm_sub_ps(v, s);
		Vector4 ret;
		_mm_storeu_ps(&ret.x, result);
		return ret;
#else
		return Vector4(x - fValue, y - fValue, z - fValue, w - fValue);
#endif
	}

	FORCEINLINE Vector4& operator-=(float fValue)
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 s = _mm_set1_ps(fValue);
		__m128 result = _mm_sub_ps(v, s);
		_mm_storeu_ps(&x, result);
#else
		x -= fValue, y -= fValue, z -= fValue, w -= fValue;
#endif
		return *this;
	}

	FORCEINLINE Vector4 operator-(Vector4 const& vector) const
	{
#if USE_SSE
		__m128 a = _mm_loadu_ps(&x);
		__m128 b = _mm_loadu_ps(&vector.x);
		__m128 result = _mm_sub_ps(a, b);
		Vector4 ret;
		_mm_storeu_ps(&ret.x, result);
		return ret;
#else
		return Vector4(x - vector.x, y - vector.y, z - vector.z, w - vector.w);
#endif
	}

	FORCEINLINE Vector4& operator-=(Vector4 const& vector)
	{
#if USE_SSE
		__m128 a = _mm_loadu_ps(&x);
		__m128 b = _mm_loadu_ps(&vector.x);
		__m128 result = _mm_sub_ps(a, b);
		_mm_storeu_ps(&x, result);
#else
		x -= vector.x, y -= vector.y, z -= vector.z, w -= vector.w;
#endif
		return *this;
	}

	FORCEINLINE Vector4 operator/(float fValue) const
	{
#pragma warning( push )
#pragma warning( disable : 4723 )
		JASSERT(!::IsNearlyZero(fValue));
		if (::IsNearlyZero(fValue))
			return Vector4(ZeroType);

#if USE_SSE
		const float RScale = 1.0f / fValue;
		__m128 v = _mm_loadu_ps(&x);
		__m128 s = _mm_set1_ps(RScale);
		__m128 result = _mm_mul_ps(v, s);
		Vector4 ret;
		_mm_storeu_ps(&ret.x, result);
		return ret;
#else
		const float RScale = 1.0f / fValue;
		return Vector4(x * RScale, y * RScale, z * RScale, w * RScale);
#endif
#pragma warning( pop )
	}

	FORCEINLINE Vector4& operator/=(float fValue)
	{
		JASSERT(!::IsNearlyZero(fValue));
		if (::IsNearlyZero(fValue))
		{
			x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
		}
		else
		{
#if USE_SSE
			const float RScale = 1.0f / fValue;
			__m128 v = _mm_loadu_ps(&x);
			__m128 s = _mm_set1_ps(RScale);
			__m128 result = _mm_mul_ps(v, s);
			_mm_storeu_ps(&x, result);
#else
			x /= fValue, y /= fValue, z /= fValue, w /= fValue;
#endif
		}
		return *this;
	}

	FORCEINLINE Vector4 operator-() const
	{
		return Vector4(-x, -y, -z, -w);
	}

	FORCEINLINE bool operator==(Vector4 const& vector) const
	{
		return (x == vector.x) && (y == vector.y) && (z == vector.z) && (w == vector.w);
	}

    FORCEINLINE bool IsNearlyEqual(Vector4 const& vector, float fTolerance = FLOAT_TOLERANCE) const
    {
        return ::IsNearlyEqual(x, vector.x, fTolerance) && ::IsNearlyEqual(y, vector.y, fTolerance) 
			&& ::IsNearlyEqual(z, vector.z, fTolerance) && ::IsNearlyEqual(w, vector.w, fTolerance);
    }

    FORCEINLINE friend bool IsNearlyEqual(Vector4 const& A, Vector4 const& B, float fTolerance = FLOAT_TOLERANCE)
    {
		return A.IsNearlyEqual(B, fTolerance);
    }

	FORCEINLINE friend Vector4 Abs(Vector4 A)
	{
		A.x = fabs(A.x);
		A.y = fabs(A.y);
		A.z = fabs(A.z);
		A.w = fabs(A.w);
		return A;
	}

	FORCEINLINE bool operator!=(Vector4 const& Vector4) const
	{
		return !(*this == Vector4);
	}

	void operator=(struct Vector2 const& vector);
	void operator=(struct Vector const& vector);

	FORCEINLINE float Length() const
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 mul = _mm_mul_ps(v, v);
		__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
		__m128 sum = _mm_add_ps(mul, shuf);
		shuf = _mm_movehl_ps(shuf, sum);
		sum = _mm_add_ss(sum, shuf);
		return sqrtf(_mm_cvtss_f32(sum));
#else
		return sqrtf(x * x + y * y + z * z + w * w);
#endif
	}

	FORCEINLINE static float Length(Vector4 const& A, Vector4 const& B)
	{
		return (A - B).Length();
	}

	FORCEINLINE float LengthSQ() const
	{
#if USE_SSE
		__m128 v = _mm_loadu_ps(&x);
		__m128 mul = _mm_mul_ps(v, v);
		__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
		__m128 sum = _mm_add_ps(mul, shuf);
		shuf = _mm_movehl_ps(shuf, sum);
		sum = _mm_add_ss(sum, shuf);
		return _mm_cvtss_f32(sum);
#else
		return x * x + y * y + z * z + w * w;
#endif
	}

	FORCEINLINE static float LengthSQ(Vector4 const& A, Vector4 const& B)
	{
		return (A - B).LengthSQ();
	}

	FORCEINLINE bool IsNearlyZero(float fTolerance = FLOAT_TOLERANCE) const
	{
		return ::IsNearlyZero(x, fTolerance) && ::IsNearlyZero(y, fTolerance) && ::IsNearlyZero(z, fTolerance) && ::IsNearlyZero(w, fTolerance);
	}

	FORCEINLINE friend bool IsNearlyZero(Vector4 const& vector, float fTolerance = FLOAT_TOLERANCE)
	{
		return vector.IsNearlyZero(fTolerance);
	}

	FORCEINLINE Vector4& SetNormalize()
	{
		if (IsNearlyZero())
		{
			JASSERT("Vector4 length is zero.");
		}
		else
		{
#if USE_SSE
			__m128 v = _mm_loadu_ps(&x);
			__m128 mul = _mm_mul_ps(v, v);
			__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
			__m128 sum = _mm_add_ps(mul, shuf);
			shuf = _mm_movehl_ps(shuf, sum);
			sum = _mm_add_ss(sum, shuf);
			__m128 len = _mm_sqrt_ss(sum);
			__m128 invLen = _mm_div_ss(_mm_set_ss(1.0f), len);
			__m128 invLenVec = _mm_shuffle_ps(invLen, invLen, _MM_SHUFFLE(0, 0, 0, 0));
			__m128 result = _mm_mul_ps(v, invLenVec);
			_mm_storeu_ps(&x, result);
#else
			float const length = 1.0f / Length();
			x *= length;
			y *= length;
			z *= length;
			w *= length;
#endif
		}

		return *this;
	}

	FORCEINLINE Vector4 GetNormalize() const
	{
		return Vector4(*this).SetNormalize();
	}

	FORCEINLINE float DotProduct(Vector4 const& vector) const
	{
		return Vector4::DotProduct(*this, vector);
	}

	FORCEINLINE static float DotProduct(Vector4 const& A, Vector4 const& B)
	{
#if USE_SSE
		__m128 a = _mm_loadu_ps(&A.x);
		__m128 b = _mm_loadu_ps(&B.x);
		__m128 mul = _mm_mul_ps(a, b);
		__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
		__m128 sum = _mm_add_ps(mul, shuf);
		shuf = _mm_movehl_ps(shuf, sum);
		sum = _mm_add_ss(sum, shuf);
		return _mm_cvtss_f32(sum);
#else
		auto const& result = A * B;
		return (result.x + result.y + result.z + result.w);
#endif
	}

	union
	{
		struct { float x, y, z, w; };
		float v[4];
	};
};

struct Vector2
{
    static const Vector2 OneVector;
    static const Vector2 ZeroVector;

	constexpr Vector2() { }
	constexpr Vector2(zero_type /*ZeroType*/) { x = 0.0f; y = 0.0f; }
	constexpr Vector2(float fValue) : x(fValue), y(fValue) { }
	Vector2(const Vector& InVector);
	Vector2(const Vector4& InVector);
	constexpr Vector2(float fX, float fY) : x(fX), y(fY) { }

	FORCEINLINE Vector2 operator*(float fValue) const
	{
		return Vector2(x * fValue, y * fValue);
	}

	/*!
	* \return Vector2
	* \param[in] vector	곱해질 벡터
	* \brief 내적과 같은 기능임.
	*/
	FORCEINLINE Vector2 operator*(Vector2 const& vector) const
	{
		return Vector2(x * vector.x, y * vector.y);
	}

	FORCEINLINE Vector2 operator+(float fValue) const
	{
		return Vector2(x + fValue, y + fValue);
	}

	FORCEINLINE Vector2 operator+(Vector2 const& vector) const
	{
		return Vector2(x + vector.x, y + vector.y);
	}

	FORCEINLINE Vector2 operator-(float fValue) const
	{
		return Vector2(x - fValue, y - fValue);
	}

	FORCEINLINE Vector2 operator-(Vector2 const& vector) const
	{
		return Vector2(x - vector.x, y - vector.y);
	}

	FORCEINLINE Vector2 operator/(float fValue) const
	{
#pragma warning( push )
#pragma warning( disable : 4723 )
        JASSERT(!::IsNearlyZero(fValue));
        if (::IsNearlyZero(fValue))
            return Vector2(ZeroType);

		const float RScale = 1.0f / fValue;
		return Vector2(x * RScale, y * RScale);
#pragma warning( pop )
	}

	FORCEINLINE Vector2 operator/(Vector2 const& vector) const
	{
        JASSERT(!IsNearlyZero(vector.x));
        JASSERT(!IsNearlyZero(vector.y));

		return Vector2(
			IsNearlyZero(vector.x) ? 0.0f : x / vector.x
			, IsNearlyZero(vector.y) ? 0.0f : y / vector.y);
	}

	FORCEINLINE Vector2& operator/=(float fValue)
	{
		JASSERT(!::IsNearlyZero(fValue));
		if (::IsNearlyZero(fValue))
			x = 0.0f, y = 0.0f;
		else
			x /= fValue, y /= fValue;
		return *this;
	}

	FORCEINLINE Vector2 operator-() const
	{
		return Vector2(-x, -y);
	}

	FORCEINLINE bool operator==(Vector2 const& vector) const
	{
		return (x == vector.x) && (y == vector.y);
	}

    FORCEINLINE bool IsNearlyEqual(Vector2 const& vector, float fTolerance = FLOAT_TOLERANCE) const
    {
        return ::IsNearlyEqual(x, vector.x, fTolerance) && ::IsNearlyEqual(y, vector.y, fTolerance);
    }

    FORCEINLINE friend bool IsNearlyEqual(Vector2 const& A, Vector2 const& B, float fTolerance = FLOAT_TOLERANCE)
    {
		return A.IsNearlyEqual(B, fTolerance);
    }

	FORCEINLINE friend Vector2 Abs(Vector2 A)
	{
		A.x = fabs(A.x);
		A.y = fabs(A.y);
		return A;
	}

	FORCEINLINE bool operator!=(Vector2 const& vector) const
	{
		return !(*this == vector);
	}

	void operator=(struct Vector const& vector);
	void operator=(struct Vector4 const& vector);

	FORCEINLINE float DotProduct(Vector2 const& vector) const
	{
		return Vector2::DotProduct(*this, vector);
	}

	FORCEINLINE static float DotProduct(Vector2 const& A, Vector2 const& B)
	{
		auto const& result = A * B;
		return result.x + result.y;
	}

	float CrossProduct(Vector2 const& vector) const
	{
		return Vector2::CrossProduct(*this, vector);
	}

	/*!
	* \return Vector2
	* \param[in] A	First Vector2
	* \param[in] B	First Vector2
	* \brief 유사 2D 외적(2D pseudo cross product). 결과값이 Scalar 이며, 크기는 Vector A와 B로 이루어진 평행사변형의 넓이.
	* 결과 Scalar가 양수이면, B가 A의 반시계방향에 그렇지 않으면 시계방향에 있다.
	*/
	FORCEINLINE static float CrossProduct(Vector2 const& A, Vector2 const& B)
	{
		auto const& result = Vector2(-A.y, A.x) * B;
		return (result.x + result.y);
	}

	FORCEINLINE float Length() const
	{
		return sqrtf(x * x + y * y);
	}

	FORCEINLINE static float Length(Vector2 const& A, Vector2 const& B)
	{
		return (A - B).Length();
	}

	FORCEINLINE float LengthSQ() const
	{
		return x * x + y * y;
	}

	FORCEINLINE static float LengthSQ(Vector2 const& A, Vector2 const& B)
	{
		return (A - B).LengthSQ();
	}

	FORCEINLINE bool IsNearlyZero(float fTolerance = FLOAT_TOLERANCE) const
	{
		return ::IsNearlyZero(x, fTolerance) && ::IsNearlyZero(y, fTolerance);
	}

	FORCEINLINE friend bool IsNearlyZero(Vector2 const& vector, float fTolerance = FLOAT_TOLERANCE)
	{
		return vector.IsNearlyZero(fTolerance);
	}

	FORCEINLINE Vector2& SetNormalize()
	{
		if (IsNearlyZero())
		{
			JASSERT("Vector2 length is zero.");
		}
		else
		{
			float const length = 1.0f / Length();
			x *= length;
			y *= length;
		}

		return *this;
	}

	FORCEINLINE Vector2 GetNormalize() const
	{
		return Vector2(*this).SetNormalize();
	}

	union
	{
		struct { float x, y; };
		float v[2];
	};
};

template <typename T>
FORCEINLINE Vector operator/(T value, Vector const& vector)
{
	JASSERT(!IsNearlyZero(vector.x));
	JASSERT(!IsNearlyZero(vector.y));
	JASSERT(!IsNearlyZero(vector.z));

	return Vector(
		IsNearlyZero(vector.x) ? 0.0f : value / vector.x
		, IsNearlyZero(vector.y) ? 0.0f : value / vector.y
		, IsNearlyZero(vector.z) ? 0.0f : value / vector.z);
}

template <typename T>
FORCEINLINE Vector operator*(T value, Vector const& vector)
{
	return Vector(vector.x * value, vector.y * value, vector.z * value);
}

template <typename T>
FORCEINLINE Vector operator-(T value, Vector const& vector)
{
	return Vector(value - vector.x, value - vector.y, value - vector.z);
}

template <typename T>
FORCEINLINE Vector operator+(T value, Vector const& vector)
{
	return Vector(vector.x + value, vector.y + value, vector.z + value);
}

struct Vector2i
{
	constexpr Vector2i() : x(0), y(0) {}
	constexpr Vector2i(int32 fX, int32 fY) : x(fX), y(fY) {}
	union
	{
		struct { int32 x, y; };
		int32 v[2];
	};
};

struct Vector3i
{
	constexpr Vector3i() : x(0), y(0), z(0) {};
	constexpr Vector3i(int32 fX, int32 fY, int32 fZ) : x(fX), y(fY), z(fZ) {}
	union
	{
		struct { int32 x, y, z; };
		int32 v[3];
	};
};

struct Vector4i
{
	constexpr Vector4i() : x(0), y(0), z(0), w(0) {}
	constexpr Vector4i(int32 fX, int32 fY, int32 fZ, int32 fW) : x(fX), y(fY), z(fZ), w(fW) {}
	union
	{
		struct { int32 x, y, z, w; };
		int32 v[4];
	};
};

//////////////////////////////////////////////////////////////////////////
// Min, Max
FORCEINLINE Vector2i Min(const Vector2i& A, const Vector2i& B)
{
    return Vector2i(Min(A.x, B.x), Min(A.y, B.y));
}

FORCEINLINE Vector2 Min(const Vector2& A, const Vector2& B)
{
    return Vector2(Min(A.x, B.x), Min(A.y, B.y));
}

FORCEINLINE Vector Min(const Vector& A, const Vector& B)
{
    return Vector(Min(A.x, B.x), Min(A.y, B.y), Min(A.z, B.z));
}

FORCEINLINE Vector4 Min(const Vector4& A, const Vector4& B)
{
    return Vector4(Min(A.x, B.x), Min(A.y, B.y), Min(A.z, B.z), Min(A.w, B.w));
}

FORCEINLINE Vector2i Max(const Vector2i& A, const Vector2i& B)
{
    return Vector2i(Max(A.x, B.x), Max(A.y, B.y));
}

FORCEINLINE Vector2 Max(const Vector2& A, const Vector2& B)
{
    return Vector2(Max(A.x, B.x), Max(A.y, B.y));
}

FORCEINLINE Vector Max(const Vector& A, const Vector& B)
{
    return Vector(Max(A.x, B.x), Max(A.y, B.y), Max(A.z, B.z));
}

FORCEINLINE Vector4 Max(const Vector4& A, const Vector4& B)
{
    return Vector4(Max(A.x, B.x), Max(A.y, B.y), Max(A.z, B.z), Max(A.w, B.w));
}
//////////////////////////////////////////////////////////////////////////
