﻿#pragma once

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
	FORCEINLINE Vector(Vector4 const& vector);
	FORCEINLINE Vector(Vector2 const& vector, float fZ);

	FORCEINLINE Vector operator*(float fValue) const
	{
		return Vector(x * fValue, y * fValue, z * fValue);
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
		x *= fValue, y *= fValue, z *= fValue;
		return *this;
	}

	FORCEINLINE Vector operator+(float fValue) const
	{
		return Vector(x + fValue, y + fValue, z + fValue);
	}

	FORCEINLINE Vector& operator+=(float fValue)
	{
		x += fValue, y += fValue, z += fValue;
		return *this;
	}
	
	FORCEINLINE Vector operator+(Vector const& vector) const
	{
		return Vector(x + vector.x, y + vector.y, z + vector.z);
	}

	FORCEINLINE Vector& operator+=(Vector const& vector)
	{
		x += vector.x, y += vector.y, z += vector.z;
		return *this;
	}

	FORCEINLINE Vector operator-(float fValue) const
	{
		return Vector(x - fValue, y - fValue, z - fValue);
	}

	FORCEINLINE Vector& operator-(float fValue)
	{
		x -= fValue, y -= fValue, z -= fValue;
		return *this;
	}

	FORCEINLINE Vector operator-(Vector const& vector) const
	{
		return Vector(x - vector.x, y - vector.y, z - vector.z);
	}

	FORCEINLINE Vector& operator-=(Vector const& vector)
	{
		x -= vector.x, y -= vector.y, z -= vector.z;
		return *this;
	}

	FORCEINLINE Vector operator/(float fValue) const
	{
		JASSERT(!IsNearlyZero(fValue));
		if (IsNearlyZero(fValue))
			return Vector(ZeroType);

		return Vector(x / fValue, y / fValue, z / fValue);
	}

	FORCEINLINE Vector& operator/=(float fValue)
	{
		JASSERT(!IsNearlyZero(fValue));
		if (IsNearlyZero(fValue))
			x = 0.0f, y = 0.0f, z = 0.0f;
		else
			x /= fValue, y /= fValue, z /= fValue;
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

	FORCEINLINE bool operator!=(Vector const& vector) const
	{
		return !(*this == vector);
	}

	FORCEINLINE void operator=(struct Vector2 const& vector);
	FORCEINLINE void operator=(struct Vector4 const& vector);

	FORCEINLINE float DotProduct(Vector const& vector) const
	{
		return Vector::DotProduct(*this, vector);
	}

	FORCEINLINE static float DotProduct(Vector const& A, Vector const& B)
	{
		auto const& result = A * B;
		return (result.x + result.y + result.z);
	}

	Vector CrossProduct(Vector const& vector) const
	{
		return Vector::CrossProduct(*this, vector);
	}

	FORCEINLINE static Vector CrossProduct(Vector const& A, Vector const& B)
	{
		return Vector(A.y * B.z - B.y * A.z, A.z * B.x - B.z * A.x, A.x * B.y - B.x * A.y);
	}

	FORCEINLINE float Length() const
	{
		return sqrtf(x * x + y * y + z * z);
	}

	FORCEINLINE static float Length(Vector const& A, Vector const& B)
	{
		return (A - B).Length();
	}

	FORCEINLINE float LengthSQ() const
	{
		return x * x + y * y + z * z;
	}

	FORCEINLINE static float LengthSQ(Vector const& A, Vector const& B)
	{
		return (A - B).LengthSQ();
	}

	FORCEINLINE bool IsZero() const
	{
		return IsNearlyZero(x) && IsNearlyZero(y) && IsNearlyZero(z);
	}

	FORCEINLINE Vector& SetNormalize()
	{
		if (IsZero())
		{
			JASSERT("Vector length is zero.");
		}
		else
		{
			float const length = 1.0f / Length();
			x *= length;
			y *= length;
			z *= length;
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
	FORCEINLINE Vector4(const Vector& InVector);
	Vector4(const Vector2& InA, const Vector2& InB);
	FORCEINLINE Vector4(Vector vector, float fW) : x(vector.x), y(vector.y), z(vector.z), w(fW) { }

	FORCEINLINE Vector4 operator*(float fValue) const
	{
		return Vector4(x * fValue, y * fValue, z * fValue, w * fValue);
	}
	
	FORCEINLINE Vector4& operator*=(float fValue)
	{
		x *= fValue, y *= fValue, z *= fValue, w *= fValue;
		return *this;
	}

	/*!
	* \return Vector4
	* \param[in] Vector4	곱해질 벡터
	* \brief 내적과 같은 기능임.
	*/
	FORCEINLINE Vector4 operator*(Vector4 const& vector) const
	{
		return Vector4(x * vector.x, y * vector.y, z * vector.z, w * vector.w);
	}

	FORCEINLINE Vector4& operator*=(Vector4 const& vector)
	{
		x *= vector.x, y *= vector.y, z *= vector.z, w *= vector.w;
		return *this;
	}

	FORCEINLINE Vector4 operator+(float fValue) const
	{
		return Vector4(x + fValue, y + fValue, z + fValue, w + fValue);
	}

	FORCEINLINE Vector4& operator+=(float fValue)
	{
		x += fValue, y += fValue, z += fValue, w += fValue;
		return *this;
	}

	FORCEINLINE Vector4 operator+(Vector4 const& vector) const
	{
		return Vector4(x + vector.x, y + vector.y, z + vector.z, w + vector.w);
	}

	FORCEINLINE Vector4& operator+=(Vector4 const& vector)
	{
		x += vector.x, y += vector.y, z += vector.z, w += vector.w;
		return *this;
	}

	FORCEINLINE Vector4 operator-(float fValue) const
	{
		return Vector4(x - fValue, y - fValue, z - fValue, w - fValue);
	}

	FORCEINLINE Vector4& operator-=(float fValue)
	{
		x -= fValue, y -= fValue, z -= fValue, w -= fValue;
		return *this;
	}

	FORCEINLINE Vector4 operator-(Vector4 const& vector) const
	{
		return Vector4(x - vector.x, y - vector.y, z - vector.z, w - vector.w);
	}

	FORCEINLINE Vector4& operator-=(Vector4 const& vector)
	{
		x -= vector.x, y -= vector.y, z -= vector.z, w -= vector.w;
		return *this;
	}

	FORCEINLINE Vector4 operator/(float fValue) const
	{
#pragma warning( push )
#pragma warning( disable : 4723 )
		JASSERT(!IsNearlyZero(fValue));
		if (IsNearlyZero(fValue))
			return Vector4(ZeroType);

		const float RScale = 1.0f / fValue;
		return Vector4(x * RScale, y * RScale, z * RScale, w * RScale);
#pragma warning( pop )
	}

	FORCEINLINE Vector4& operator/=(float fValue)
	{
		JASSERT(!IsNearlyZero(fValue));
		if (IsNearlyZero(fValue))
			x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
		else
			x /= fValue, y /= fValue, z /= fValue, w /= fValue;
		return *this;
	}

	FORCEINLINE Vector4 operator-() const
	{
		return Vector4(-x, -y, -z, -w);
	}

	FORCEINLINE bool operator==(Vector4 const& vector) const
	{
		return IsNearlyEqual(x, vector.x) && IsNearlyEqual(y, vector.y) && IsNearlyEqual(z, vector.z) && IsNearlyEqual(w, vector.w);
	}

	FORCEINLINE bool operator!=(Vector4 const& Vector4) const
	{
		return !(*this == Vector4);
	}

	FORCEINLINE void operator=(struct Vector2 const& vector);
	FORCEINLINE void operator=(struct Vector const& vector);

	FORCEINLINE float Length() const
	{
		return sqrtf(x * x + y * y + z * z + w * w);
	}

	FORCEINLINE static float Length(Vector4 const& A, Vector4 const& B)
	{
		return (A - B).Length();
	}

	FORCEINLINE float LengthSQ() const
	{
		return x * x + y * y + z * z + w * w;
	}

	FORCEINLINE static float LengthSQ(Vector4 const& A, Vector4 const& B)
	{
		return (A - B).LengthSQ();
	}

	FORCEINLINE bool IsZero() const
	{
		return IsNearlyZero(x) && IsNearlyZero(y) && IsNearlyZero(z) && IsNearlyZero(w);
	}

	FORCEINLINE Vector4& SetNormalize()
	{
		if (IsZero())
		{
			JASSERT("Vector4 length is zero.");
		}
		else
		{
			float const length = 1.0f / Length();
			x *= length;
			y *= length;
			z *= length;
			w *= length;
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
		auto const& result = A * B;
		return (result.x + result.y + result.z + result.w);
	}

	union
	{
		struct { float x, y, z, w; };
		float v[4];
	};
};

struct Vector2
{
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
        JASSERT(!IsNearlyZero(fValue));
        if (IsNearlyZero(fValue))
            return Vector2(ZeroType);

		const float RScale = 1.0f / fValue;
		return Vector2(x * RScale, y * RScale);
#pragma warning( pop )
	}

	FORCEINLINE Vector2 operator/(Vector2 const& vector) const
	{
        JASSERT(IsNearlyZero(vector.x));
        JASSERT(IsNearlyZero(vector.y));

		return Vector2(
			IsNearlyZero(vector.x) ? 0.0f : x / vector.x
			, IsNearlyZero(vector.y) ? 0.0f : y / vector.y);
	}

	FORCEINLINE Vector2& operator/=(float fValue)
	{
		JASSERT(!IsNearlyZero(fValue));
		if (IsNearlyZero(fValue))
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
		return IsNearlyEqual(x, vector.x) && IsNearlyEqual(y, vector.y);
	}

	FORCEINLINE bool operator!=(Vector2 const& vector) const
	{
		return !(*this == vector);
	}

	FORCEINLINE void operator=(struct Vector const& vector);
	FORCEINLINE void operator=(struct Vector4 const& vector);

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

	FORCEINLINE bool IsZero() const
	{
		return IsNearlyZero(x) && IsNearlyZero(y);
	}

	FORCEINLINE Vector2& SetNormalize()
	{
		if (IsZero())
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

Vector::Vector(Vector4 const& vector)
	: x(vector.x), y(vector.y), z(vector.z)
{

}

Vector::Vector(Vector2 const& vector, float fZ)
	: x(vector.x), y(vector.y), z(fZ)
{
}

template <typename T>
FORCEINLINE Vector operator/(T value, Vector const& vector)
{
	JASSERT(IsNearlyZero(vector.x));
	JASSERT(IsNearlyZero(vector.y));
	JASSERT(IsNearlyZero(vector.z));

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
	constexpr Vector2i() = default;
	constexpr Vector2i(int32 fX, int32 fY) : x(fX), y(fY) {}
	union
	{
		struct { int32 x, y; };
		int32 v[2];
	};
};

struct Vector3i
{
	constexpr Vector3i() = default;
	constexpr Vector3i(int32 fX, int32 fY, int32 fZ) : x(fX), y(fY), z(fZ) {}
	union
	{
		struct { int32 x, y, z; };
		int32 v[3];
	};
};

struct Vector4i
{
	constexpr Vector4i() = default;
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
