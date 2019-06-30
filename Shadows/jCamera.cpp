#include "pch.h"
#include "jCamera.h"
#include "jLight.h"

std::map<int32, jCamera*> jCamera::CameraMap;

jCamera::jCamera()
{
}


jCamera::~jCamera()
{
}

void jCamera::UpdateCameraFrustum()
{
	auto toTarget = (Target - Pos).GetNormalize();
	const auto length = tanf(FOV) * Far;
	auto toRight = GetRightVector() * length;
	auto toUp = GetUpVector() * length;

	auto rightUp = (toTarget * Far + toRight + toUp).GetNormalize();
	auto leftUp = (toTarget * Far - toRight + toUp).GetNormalize();
	auto rightDown = (toTarget * Far + toRight - toUp).GetNormalize();
	auto leftDown = (toTarget * Far - toRight - toUp).GetNormalize();

	auto far_lt = Pos + leftUp * Far;
	auto far_rt = Pos + rightUp * Far;
	auto far_lb = Pos + leftDown * Far;
	auto far_rb = Pos + rightDown * Far;

	auto near_lt = Pos + leftUp * Near;
	auto near_rt = Pos + rightUp * Near;
	auto near_lb = Pos + leftDown * Near;
	auto near_rb = Pos + rightDown * Near;

	Frustum.Planes[0] = jPlane::CreateFrustumFromThreePoints(near_lb, far_lb, near_lt);		// left
	Frustum.Planes[1] = jPlane::CreateFrustumFromThreePoints(near_rt, far_rt, near_rb);		// right
	Frustum.Planes[2] = jPlane::CreateFrustumFromThreePoints(near_lt, far_lt, near_rt);		// top
	Frustum.Planes[3] = jPlane::CreateFrustumFromThreePoints(near_rb, far_rb, near_lb);		// bottom
	Frustum.Planes[4] = jPlane::CreateFrustumFromThreePoints(near_lb, near_lt, near_rb);	// near
	Frustum.Planes[5] = jPlane::CreateFrustumFromThreePoints(far_rb, far_rt, far_lb);		// far

	// debug object update
}

void jCamera::UpdateCamera()
{
	View = jCameraUtil::CreateViewMatrix(Pos, Target, Up);

	if (IsPerspectiveProjection)
	{
		 Projection = jCameraUtil::CreatePerspectiveMatrix(Width, Height, FOV, Far, Near);
		//Projection = jCameraUtil::CreatePerspectiveMatrixFarAtInfinity(Width, Height, FOV, Near);
	}
	else
	{
		Projection = jCameraUtil::CreateOrthogonalMatrix(Width, Height, Far, Near);
	}
}

void jCamera::AddLight(jLight* light)
{
	if (light)
	{
		LightList.push_back(light);
		if (light->Type == ELightType::AMBIENT)
			Ambient = static_cast<jAmbientLight*>(light);
	}
}

jLight* jCamera::GetLight(int32 index) const
{
	JASSERT(index >= 0 && LightList.size() > index);
	return LightList[index];
}

jLight* jCamera::GetLight(ELightType type) const
{
	for (auto& iter : LightList)
	{
		if (iter->Type == type)
			return iter;
	}
	return nullptr;
}

int32 jCamera::GetNumOfLight() const
{
	return static_cast<int32>(LightList.size());
}

Matrix jCameraUtil::CreateViewMatrix(const Vector& pos, const Vector& target, const Vector& up)
{
	const auto zAxis = (target - pos).GetNormalize();
	auto yAxis = (up - pos).GetNormalize();
	const auto xAxis = zAxis.CrossProduct(yAxis).GetNormalize();
	yAxis = xAxis.CrossProduct(zAxis).GetNormalize();

	Matrix InvRot{ IdentityType };
	InvRot.m[0][0] = xAxis.x;
	InvRot.m[0][1] = xAxis.y;
	InvRot.m[0][2] = xAxis.z;
	InvRot.m[1][0] = yAxis.x;
	InvRot.m[1][1] = yAxis.y;
	InvRot.m[1][2] = yAxis.z;
	InvRot.m[2][0] = -zAxis.x;
	InvRot.m[2][1] = -zAxis.y;
	InvRot.m[2][2] = -zAxis.z;

	// auto InvPos = Matrix::MakeTranslate(-pos.x, -pos.y, -pos.z);
	// return InvRot * InvPos;

	auto InvPos = Vector4(-pos.x, -pos.y, -pos.z, 1.0);
	InvRot.m[0][3] = InvRot.GetRow(0).DotProduct(InvPos);
	InvRot.m[1][3] = InvRot.GetRow(1).DotProduct(InvPos);
	InvRot.m[2][3] = InvRot.GetRow(2).DotProduct(InvPos);
	return InvRot;
}

Matrix jCameraUtil::CreatePerspectiveMatrix(float width, float height, float fov, float farDist, float nearDist)
{
	const float F = 1.0f / tanf(fov / 2.0f);
	const float farSubNear = (farDist - nearDist);

	Matrix projMat;
	projMat.m[0][0] = F * (height / width); projMat.m[0][1] = 0.0f;      projMat.m[0][2] = 0.0f;									projMat.m[0][3] = 0.0f;
	projMat.m[1][0] = 0.0f;					projMat.m[1][1] = F;         projMat.m[1][2] = 0.0f;									projMat.m[1][3] = 0.0f;
	projMat.m[2][0] = 0.0f;					projMat.m[2][1] = 0.0f;      projMat.m[2][2] = -(farDist + nearDist) / farSubNear;		projMat.m[2][3] = -(2.0f*nearDist*farDist) / farSubNear;
	projMat.m[3][0] = 0.0f;					projMat.m[3][1] = 0.0f;      projMat.m[3][2] = -1.0f;									projMat.m[3][3] = 0.0f;
	return projMat;
}

Matrix jCameraUtil::CreatePerspectiveMatrixFarAtInfinity(float width, float height, float fov, float nearDist)
{
	const float F = 1.0f / tanf(fov / 2.0f);

	Matrix projMat;
	projMat.m[0][0] = F * (height / width); projMat.m[0][1] = 0.0f;      projMat.m[0][2] = 0.0f;                      projMat.m[0][3] = 0.0f;
	projMat.m[1][0] = 0.0f;					projMat.m[1][1] = F;         projMat.m[1][2] = 0.0f;                      projMat.m[1][3] = 0.0f;
	projMat.m[2][0] = 0.0f;					projMat.m[2][1] = 0.0f;      projMat.m[2][2] = -1.0f;                     projMat.m[2][3] = -(2.0f*nearDist);
	projMat.m[3][0] = 0.0f;					projMat.m[3][1] = 0.0f;      projMat.m[3][2] = -1.0f;                     projMat.m[3][3] = 0.0f;
	return projMat;
}

Matrix jCameraUtil::CreateOrthogonalMatrix(float width, float height, float farDist, float nearDist)
{
	const float farSubNear = (farDist - nearDist);

	Matrix projMat;
	projMat.m[0][0] = 1.0f / width;      projMat.m[0][1] = 0.0f;                  projMat.m[0][2] = 0.0f;                      projMat.m[0][3] = 0.0f;
	projMat.m[1][0] = 0.0f;              projMat.m[1][1] = 1.0f / height;         projMat.m[1][2] = 0.0f;                      projMat.m[1][3] = 0.0f;
	projMat.m[2][0] = 0.0f;              projMat.m[2][1] = 0.0f;                  projMat.m[2][2] = -2.0f / farSubNear;        projMat.m[2][3] = -(farDist + nearDist) / farSubNear;
	projMat.m[3][0] = 0.0f;              projMat.m[3][1] = 0.0f;                  projMat.m[3][2] = 0.0f;                      projMat.m[3][3] = 1.0f;
	return projMat;
}
