﻿#pragma once

#include "Math\Matrix.h"
#include "Math\Vector.h"
#include "Math\Plane.h"

//class jLight;
//class jAmbientLight;

struct jFrustumPlane
{
	bool IsInFrustum(const Vector& pos, float radius) const
	{
		for (const auto& plane : Planes)
		{
			const float r = pos.DotProduct(plane.n) - plane.d + radius;
			if (r < 0.0f)
				return false;
		}
		return true;
	}

	bool IsInFrustumWithDirection(const Vector& pos, const Vector& direction, float radius) const
	{
		for (const auto& plane : Planes)
		{
			const float r = pos.DotProduct(plane.n) - plane.d + radius;
			if (r < 0.0f)
			{
				if (direction.DotProduct(plane.n) <= 0.0)
					return false;
			}
		}
		return true;
	}

	std::array<jPlane, 6> Planes;
};

namespace jCameraUtil
{
	Matrix CreateViewMatrix(const Vector& pos, const Vector& target, const Vector& up);

	Matrix CreatePerspectiveMatrix(float width, float height, float fov, float nearDist, float farDist);
	Matrix CreatePerspectiveMatrixFarAtInfinity(float width, float height, float fov, float nearDist);
	Matrix CreateReverseZPerspectiveMatrix(float width, float height, float fov, float nearDist, float farDist);		// For the shadow map

	Matrix CreateOrthogonalMatrix(float width, float height, float nearDist, float farDist);
	Matrix CreateOrthogonalMatrix(float left, float right, float top, float bottom, float nearDist, float farDist);
}

class jCamera
{
public:
	enum ECameraType
	{
		NORMAL = 0,
		ORTHO,
		MAX
	};

	static std::map<int32, jCamera*> CameraMap;
	FORCEINLINE static void AddCamera(int32 id, jCamera* camera)
	{
		CameraMap.insert(std::make_pair(id, camera));
	}
	FORCEINLINE static jCamera* GetCamera(int32 id)
	{
		auto it_find = CameraMap.find(id);
		return (CameraMap.end() != it_find) ? it_find->second : nullptr;
	}
	FORCEINLINE static void RemoveCamera(int32 id)
	{
		CameraMap.erase(id);
	}
	FORCEINLINE static jCamera* GetMainCamera()
	{
		return GetCamera(0);
	}
	FORCEINLINE static jCamera* CreateCamera(const Vector& pos, const Vector& target, const Vector& up
		, float fovRad, float nearDist, float farDist, float width, float height, bool isPerspectiveProjection)
	{
		jCamera* camera = new jCamera();
		SetCamera(camera, pos, target, up, fovRad, nearDist, farDist, width, height, isPerspectiveProjection);
		return camera;
	}
	FORCEINLINE static void GetForwardRightUpFromEulerAngle(Vector& OutForward, Vector& OutRight, Vector& OutUp, const Vector& InEulerAngle)
	{
		OutForward = InEulerAngle.GetDirectionFromEulerAngle().GetNormalize();

		const bool IsInvert = (InEulerAngle.x < 0 || PI < InEulerAngle.x);

		const Vector UpVector = (IsInvert ? -Vector::UpVector : Vector::UpVector);
		OutRight = (IsInvert ? -Vector::UpVector : Vector::UpVector).CrossProduct(OutForward).GetNormalize();
		if (IsNearlyZero(OutRight.LengthSQ()))
			OutRight = (IsInvert ? -Vector::FowardVector : Vector::FowardVector).CrossProduct(OutForward).GetNormalize();
		OutUp = OutForward.CrossProduct(OutRight).GetNormalize();
	}
	FORCEINLINE static void SetCamera(jCamera* OutCamera, const Vector& pos, const Vector& target, const Vector& up
		, float fovRad, float nearDist, float farDist, float width, float height, bool isPerspectiveProjection, float distance = 300.0f)
	{
		const auto toTarget = (target - pos);
		OutCamera->Pos = pos;
        OutCamera->Target = target;
        OutCamera->Up = up;
        OutCamera->Distance = distance;
		OutCamera->SetEulerAngle(Vector::GetEulerAngleFrom(toTarget));

		OutCamera->FOVRad = fovRad;
		OutCamera->Near = nearDist;
		OutCamera->Far = farDist;
		OutCamera->Width = static_cast<int32>(width);
		OutCamera->Height = static_cast<int32>(height);
		OutCamera->IsPerspectiveProjection = isPerspectiveProjection;
	}

	jCamera();
	virtual ~jCamera();

	virtual Matrix CreateView() const;
	virtual Matrix CreateProjection() const;

	virtual void BindCamera(const jShader* shader) const;

	void UpdateCameraFrustum();
	void UpdateCamera();

	FORCEINLINE void UpdateCameraParameters()
	{
		Vector ForwardDir;
		Vector RightDir;
		Vector UpDir;
		GetForwardRightUpFromEulerAngle(ForwardDir, RightDir, UpDir, EulerAngle);
		Target = Pos + ForwardDir * Distance;
		Up = Pos + UpDir;
	}

	FORCEINLINE void SetEulerAngle(const Vector& InEulerAngle)
	{
		if (!EulerAngle.IsNearlyEqual(InEulerAngle))
		{
			EulerAngle = InEulerAngle;
			UpdateCameraParameters();
		}
	}

	FORCEINLINE Vector GetForwardVector() const
	{
		return View.GetColumn(2);
	}

	FORCEINLINE Vector GetUpVector() const
	{
		return View.GetColumn(1);
	}

	FORCEINLINE Vector GetRightVector() const
	{
		return View.GetColumn(0);
	}

	FORCEINLINE void MoveShift(float dist)
	{
		auto toRight = GetRightVector() * dist;
		Pos += toRight;
		Target += toRight;
		Up += toRight;
	}

	FORCEINLINE void MoveForward(float dist)
	{
		auto toForward = GetForwardVector() * dist;
		Pos += toForward;
		Target += toForward;
		Up += toForward;
	}

	FORCEINLINE void RotateCameraAxis(const Vector& axis, float radian)
	{
		const auto transformMatrix = Matrix::MakeTranslate(Pos) * Matrix::MakeRotate(axis, radian) * Matrix::MakeTranslate(-Pos);
		Pos = transformMatrix.TransformPoint(Pos);
		Target = transformMatrix.TransformPoint(Target);
		Up = transformMatrix.TransformPoint(Up);
	}

	FORCEINLINE void RotateForwardAxis(float radian)
	{
		RotateCameraAxis(GetForwardVector(), radian);
	}

	FORCEINLINE void RotateUpAxis(float radian)
	{
		RotateCameraAxis(GetUpVector(), radian);
	}

	FORCEINLINE void RotateRightAxis(float radian)
	{
		RotateCameraAxis(GetRightVector(), radian);
	}

	FORCEINLINE void RotateXAxis(float radian)
	{
		RotateCameraAxis(Vector(1.0f, 0.0f, 0.0f), radian);
	}

	FORCEINLINE void RotateYAxis(float radian)
	{
		RotateCameraAxis(Vector(0.0f, 1.0f, 0.0f), radian);
	}

	FORCEINLINE void RotateZAxis(float radian)
	{
		RotateCameraAxis(Vector(0.0f, 0.0f, 1.0f), radian);
	}

	FORCEINLINE Matrix GetViewProjectionMatrix() const
	{
		return ViewProjection;
	}

    FORCEINLINE Matrix GetInverseViewProjectionMatrix() const
    {
        return InvViewProjection;
    }

	FORCEINLINE bool IsInFrustum(const Vector& pos, float radius)
	{
		for (auto& iter : Frustum.Planes)
		{
			const float d = pos.DotProduct(iter.n) - iter.d + radius;
			if (d < 0.0f)
				return false;
		}
		return true;
	}

	FORCEINLINE bool IsInFrustumWithDirection(const Vector& pos, const Vector& dir, float radius) const
	{
		for (auto& iter : Frustum.Planes)
		{
			const float d = pos.DotProduct(iter.n) - iter.d + radius;
			if (d < 0.0f)
			{
				if (dir.DotProduct(iter.n) <= 0)
					return false;
			}
		}
		return true;
	}

	FORCEINLINE Vector GetEulerAngle() const { return EulerAngle; }

	void GetRectInNDCSpace(Vector& OutPosMin, Vector& OutPosMax, const Matrix& InVP) const;
	void GetRectInScreenSpace(Vector& OutPosMin, Vector& OutPosMax, const Matrix& InVP, const Vector2& InScreenSize = Vector2(1.0f, 1.0f)) const;
	void GetFrustumVertexInWorld(Vector* OutVertexArray) const;

	//void AddLight(jLight* light);
	//jLight* GetLight(int32 index) const;
	//jLight* GetLight(ELightType type) const;
	//void RemoveLight(int32 index);
	//void RemoveLight(ELightType type);
	//void RemoveLight(jLight* light);

	//int32 GetNumOfLight() const;

	// Camera Uniform buffer
	//struct jUniformBufferCamera
	//{
	//	Matrix VP;
	//	Matrix V;
	//	float Near;
	//	float Far;
	//};
	//jUniformBufferCamera Data;
	//IUniformBufferBlock* UniformBufferBlock = nullptr;

	//void UpdateUniformBuffer()
	//{
	//	if (!UniformBufferBlock)
	//	{
	//		UniformBufferBlock = g_rhi->CreateUniformBufferBlock("Camera", sizeof(jUniformBufferCamera));
	//	}

	//	const auto& V = View;
	//	const auto& VP = Projection * View;

	//	jUniformBufferCamera ubo = {};
	//	ubo.V = V;
	//	ubo.VP = VP;
	//	ubo.Far = Far;
	//	ubo.Near = Near;

	//	UniformBufferBlock->UpdateBufferData(&ubo, sizeof(ubo));
	//}
	//////////////////////////////////////////////////////////////////////////

	ECameraType Type;

	Vector Pos;
	Vector Target;
	Vector Up;

	Vector EulerAngle = Vector::ZeroVector;
	float Distance = 300.0f;
	
	Matrix View;
	Matrix Projection;
	Matrix ViewProjection;
	Matrix InvViewProjection;
	Matrix PrevViewProjection;
	Matrix ReverseZProjection;
	bool IsPerspectiveProjection = true;
	bool IsInfinityFar = false;

	// debug object

	float FOVRad = 0.0f;		// Radian
	float Near = 0.0f;
	float Far = 0.0f;

	//std::vector<jLight*> LightList;
	//jAmbientLight* Ambient = nullptr;
	bool UseAmbient = false;
	jFrustumPlane Frustum;
	int32 Width = 0;
	int32 Height = 0;

	// todo 현재 렌더 스테이트를 저장하는 객체로 옮길 예정
	bool IsEnableCullMode = false;
	float PCF_SIZE_DIRECTIONAL = 2.0f;
	float PCF_SIZE_OMNIDIRECTIONAL = 8.0f;

	// todo 현재는 레이 트레이싱의 DOF 구현에만 사용됨.
	float Aperture = 0.0f;
	float FocalDist = 0.0f;
};

class jOrthographicCamera : public jCamera 
{
public:
	FORCEINLINE static jOrthographicCamera* CreateCamera(const Vector& pos, const Vector& target, const Vector& up
		, float minX, float minY, float maxX, float maxY, float nearDist, float farDist)
	{
		jOrthographicCamera* camera = new jOrthographicCamera();
		SetCamera(camera, pos, target, up, minX, minY, maxX, maxY, nearDist, farDist);
		return camera;
	}

	FORCEINLINE static void SetCamera(jOrthographicCamera* OutCamera, const Vector& pos, const Vector& target, const Vector& up
		, float minX, float minY, float maxX, float maxY, float nearDist, float farDist, float distance = 300.0f)
	{
		const auto toTarget = (target - pos);
		OutCamera->Pos = pos;
		OutCamera->Target = target;
		OutCamera->Up = up;
		OutCamera->Distance = distance;
		OutCamera->SetEulerAngle(Vector::GetEulerAngleFrom(toTarget));

		OutCamera->Near = nearDist;
		OutCamera->Far = farDist;
		OutCamera->IsPerspectiveProjection = false;

		OutCamera->MinX = minX;
		OutCamera->MinY = minY;
		OutCamera->MaxX = maxX;
		OutCamera->MaxY = maxY;
	}

	jOrthographicCamera()
	{
		Type = ECameraType::ORTHO;
	}

	virtual Matrix CreateProjection() const;
	
	float GetMinX() const { return MinX; };
	float GetMinY() const { return MinY; };
	float GetMaxX() const { return MaxX; };
	float GetMaxY() const { return MaxY; };

	void SetMinX(float minX) { MinX = minX; }
	void SetMinY(float minY) { MinY = minY; }
	void SetMaxX(float maxX) { MaxX = maxX; }
	void SetMaxY(float maxY) { MaxY = maxY; }

private:
	float MinX = 0.0f;
	float MinY = 0.0f;

	float MaxX = 0.0f;
	float MaxY = 0.0f;
};