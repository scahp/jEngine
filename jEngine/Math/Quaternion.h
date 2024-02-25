#pragma once

/*!
 * \file Quaternion.h
 * \date 2024/2/25 19:25
 *
 * \author 최재호(scahp)
 * Contact: scahp@naver.com
 *
 * \brief Quaternion 으로 회전을 정의 하는 클래스
*/

#include <math.h>
#include "MathUtility.h"
#include "Generic/TemplateUtility.h"
#include "Vector.h"
#include "Matrix.h"

struct Quaternion
{
	static const Quaternion Identify;

	FORCEINLINE static Quaternion CreateRotation(Vector const& InAxis, float InRadian) { Quaternion NewQuat; NewQuat.SetRotation(InAxis, InRadian); return NewQuat; }
	FORCEINLINE static Quaternion CreateRotation(Matrix const& InMatrix) { Quaternion NewQuat; NewQuat.SetRotation(InMatrix); return NewQuat; }

	FORCEINLINE constexpr Quaternion() { }
	FORCEINLINE constexpr Quaternion(zero_type /*ZeroType*/) { x = 0.0f; y = 0.0f; z = 0.0f; w = 0.0f; }
	FORCEINLINE constexpr Quaternion(float fX, float fY, float fZ, float fW) : x(fX), y(fY), z(fZ), w(fW) { }
	Quaternion(Vector const& vector) { x = vector.x; y = vector.y; z = vector.z; w = 0.0f; }
	Quaternion(Vector4 const& vector) { x = vector.x; y = vector.y; z = vector.z; w = vector.w; }

    FORCEINLINE Quaternion operator*(Quaternion const& quat) const
    {
        return Quaternion(
			quat.w * x + quat.x * w + quat.y * z - quat.z * y
			, quat.w * y - quat.x * z + quat.y * w + quat.z * x
			, quat.w * z + quat.x * y - quat.y * x + quat.z * w
			, quat.w * w - quat.x * x - quat.y * y - quat.z * z);
    }
    
	FORCEINLINE Quaternion operator*(float value) const
	{
		return Quaternion(x * value
			, y * value
			, z * value
			, w * value);
	}

    FORCEINLINE Quaternion operator/(float value) const
    {
		check(!IsNearlyZero(value));
		const float InvValue = 1.0f / value;
        return Quaternion(x * InvValue
            , y * InvValue
            , z * InvValue
            , w * InvValue);
    }

	FORCEINLINE void SetInverse()
	{
		x = -x; y = -y; z = -z;
	}

	FORCEINLINE Quaternion GetConjugate() const
	{
		return Quaternion(-x, -y, -z, w);
	}

	FORCEINLINE Quaternion GetInverse() const
	{
		return GetConjugate() / GetLengthSq();
	}

    FORCEINLINE float GetLengthSq() const
    {
        return (x * x + y * y + z * z + w * w);
    }

	FORCEINLINE float GetLength() const
	{
		return sqrt(x * x + y * y + z * z + w * w);
	}

	FORCEINLINE void SetNormalize()
	{
		float Length = GetLength();
		check(!IsNearlyZero(Length));

		x /= Length; y /= Length; z /= Length; w /= Length;
	}

	FORCEINLINE Quaternion GetNormalize() const
	{
        float Length = GetLength();
        check(!IsNearlyZero(Length));

		return Quaternion(x / Length, y / Length, z / Length, w / Length);
	}

	FORCEINLINE void SetRotation(Vector const& InAxis, float InRadian)
	{
		const float HalfRandian = InRadian * 0.5f;
		w = cos(HalfRandian);

		const Vector Result = InAxis.GetNormalize()
#if LEFT_HANDED
			* sin(-HalfRandian);
#elif RIGHT_HANDED
			* sin(HalfRandian);
#endif
		x = Result.x; y = Result.y; z = Result.z;
	}

	FORCEINLINE Quaternion GetRotation(Vector const& InAxis, float InRadian)
	{
		Quaternion NewQuat{};
        const float HalfRandian = InRadian * 0.5f;
		NewQuat.w = cos(HalfRandian);

        const Vector Result = InAxis.GetNormalize()
#if LEFT_HANDED
            * sin(-HalfRandian);
#elif RIGHT_HANDED
            * sin(HalfRandian);
#endif
		NewQuat.x = Result.x; NewQuat.y = Result.y; NewQuat.z = Result.z;
		return NewQuat;
	}

	FORCEINLINE Vector TransformDirection(Vector const& InVector) const
	{
		Quaternion RotatedVector = *this * Quaternion(InVector) * GetConjugate();
		return Vector(RotatedVector.x, RotatedVector.y, RotatedVector.z);
	}

	FORCEINLINE Matrix GetMatrix() const
	{
		return Matrix(
			(1.0f - 2.0f * y * y - 2.0f * z * z), (2.0f * x * y - 2.0f * w * z), (2.0f * x * z + 2.0f * w * y), 0.0f,
			(2.0f * x * y + 2.0f * w * z), (1.0f - 2.0f * x * x - 2.0f * z * z), (2.0f * y * z - 2.0f * w * x), 0.0f,
			(2.0f * x * z - 2.0f * w * y), (2.0f * y * z + 2.0f * w * x), (1.0f - 2.0f * x * x - 2.0f * y * y), 0.0f,
			0.0f, 0.0f, 0.0f, 0.0f
		);
	}

	FORCEINLINE void SetRotation(Matrix const& matrix)
	{
		w = sqrt(matrix.m00 + matrix.m11 + matrix.m22 + 1.0f) / 2.0f;
		if (w > 0.0f)
		{
			const float w4 = 4.0f * w;
			x = (matrix.m21 - matrix.m12) / w4;
			y = (matrix.m02 - matrix.m20) / w4;
			z = (matrix.m10 - matrix.m01) / w4;
		}
		else
		{
			if (matrix.m00 > matrix.m11 && matrix.m00 > matrix.m22)			// m00 is the biggest
			{
				x = sqrt(1.0f + matrix.m00 - matrix.m11 - matrix.m22);
				
				const float x4 = 4.0f * x;
				w = (matrix.m21 - matrix.m12) / x4;
				y = (matrix.m10 + matrix.m01) / x4;
				z = (matrix.m02 + matrix.m20) / x4;
			}
            else if (matrix.m11 > matrix.m00 && matrix.m11 > matrix.m22)	// m11 is the biggest
            {
				y = sqrt(1.0f + matrix.m11 - matrix.m00 - matrix.m22);

				const float y4 = 4.0f * y;
				w = (matrix.m02 - matrix.m20) / y4;
				x = (matrix.m10 + matrix.m01) / y4;
				z = (matrix.m21 + matrix.m12) / y4;
            }
            else if (matrix.m22 > matrix.m00 && matrix.m22 > matrix.m11)	// m22 is the biggest
            {
                z = sqrt(1.0f + matrix.m22 - matrix.m00 - matrix.m11);

                const float z4 = 4.0f * z;
				w = (matrix.m10 - matrix.m01) / z4;
				x = (matrix.m02 + matrix.m20) / z4;
				y = (matrix.m21 + matrix.m12) / z4;
            }
			else
			{
				check(0);
			}
		}
	}

	Quaternion GetRotation(Matrix const& matrix)
	{
		Quaternion NewQuat(*this);
		NewQuat.SetRotation(matrix);
		return NewQuat;
	}

	// xyz is imaginary part, w is real part
	union
	{
		struct { float x, y, z, w; };
		float v[4];
	};	
};

// Unit test form Quaternion
struct Quaternion_UnitTest
{
	bool operator()() const
	{
        const float radian = DegreeToRadian(90.0f);

        // Test Matrix MX, MY, MZ
        Matrix MX = Matrix::MakeRotateX(radian);
        Matrix MY = Matrix::MakeRotateY(radian);
        Matrix MZ = Matrix::MakeRotateZ(radian);

        // Test Quaternion QX, QY, QZ
        Quaternion QX;
        QX.SetRotation(Vector(1.0f, 0.0f, 0.0f), radian);
        Quaternion QY;
        QY.SetRotation(Vector(0.0f, 1.0f, 0.0f), radian);
        Quaternion QZ;
        QZ.SetRotation(Vector(0.0f, 0.0f, 1.0f), radian);

        // Test Vector A
        Vector A(1.0f, 0.0f, 0.0f);

        // 1. CaseA. Comapre between Matrix and Quaternion
        // 1.1. Transform by using Matrix
        Vector A_MX = MX.TransformDirection(A);
        Vector A_MY = MY.TransformDirection(A);
        Vector A_MZ = MZ.TransformDirection(A);

        // 1.2. Transform by using Quaternion
        Vector A_QX = QX.TransformDirection(A);
        Vector A_QY = QY.TransformDirection(A);
        Vector A_QZ = QZ.TransformDirection(A);

		if (!ensure(IsNearlyEqual(A_MX, A_QX))) return false;
		if (!ensure(IsNearlyEqual(A_MY, A_QY))) return false;
		if (!ensure(IsNearlyEqual(A_MZ, A_QZ))) return false;

        // 2. CaseB, Compare between Matrix generated by Quaternion and Matrix
        // 2.1. Generate Matrix from Quaternion
        Matrix QMX = QX.GetMatrix();
        Matrix QMY = QY.GetMatrix();
        Matrix QMZ = QZ.GetMatrix();

        Vector A_QMX = QMX.TransformDirection(A);
        Vector A_QMY = QMY.TransformDirection(A);
        Vector A_QMZ = QMZ.TransformDirection(A);

        if (!ensure(IsNearlyEqual(A_MX, A_QMX))) return false;
        if (!ensure(IsNearlyEqual(A_MY, A_QMY))) return false;
		if (!ensure(IsNearlyEqual(A_MZ, A_QMZ))) return false;

        // 3. CaseC, Compare between Quaternion generated by Matrix and Quaternion
        Quaternion MQX;
        MQX.SetRotation(MX);
        Quaternion MQY;
        MQY.SetRotation(MY);
        Quaternion MQZ;
        MQZ.SetRotation(MZ);

        Vector A_MQX = MQX.TransformDirection(A);
        Vector A_MQY = MQY.TransformDirection(A);
        Vector A_MQZ = MQZ.TransformDirection(A);

        if (!ensure(IsNearlyEqual(A_MX, A_MQX))) return false;
        if (!ensure(IsNearlyEqual(A_MY, A_MQY))) return false;
		if (!ensure(IsNearlyEqual(A_MZ, A_MQZ))) return false;

		return true;
	}
};