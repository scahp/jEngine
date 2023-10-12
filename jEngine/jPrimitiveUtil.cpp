﻿#include "pch.h"
#include "jPrimitiveUtil.h"
#include "Math/Vector.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Math/Plane.h"
#include "FileLoader/jImageFileLoader.h"
#include "Material/jMaterial.h"

struct Triangle
{
	int32 Index[3];
};

void CalculateTangents(Vector4* OutTangentArray, int32 InTriangleCount, const Triangle* InTriangleArray, int32 InVertexCount
	, const Vector* InVertexArray, const Vector* InNormalArray, const Vector2* InTexCoordArray)
{
	// 임시버퍼 생성
	Vector* Tangent = new Vector[InVertexCount * 2];
	Vector* Bitangent = Tangent + InVertexCount;
	for (int32 i = 0; i < InVertexCount; ++i)
	{
		Tangent[i] = Vector::ZeroVector;
		Bitangent[i] = Vector::ZeroVector;
	}

	// 모든 삼각형에 대해서 Tangent와 Bitangent를 계산하고 삼각형의 3개의 Vertex에 Tangent와 Bitangent를 더해줍니다.
	for (int32 k = 0; k < InTriangleCount; ++k)
	{
		int32 i0 = InTriangleArray[k].Index[0];
		int32 i1 = InTriangleArray[k].Index[1];
		int32 i2 = InTriangleArray[k].Index[2];

		const Vector& p0 = InVertexArray[i0];
		const Vector& p1 = InVertexArray[i1];
		const Vector& p2 = InVertexArray[i2];
		const Vector2& w0 = InTexCoordArray[i0];
		const Vector2& w1 = InTexCoordArray[i1];
		const Vector2& w2 = InTexCoordArray[i2];

		Vector e1 = p1 - p0;
		Vector e2 = p2 - p0;
		float x1 = w1.x - w0.x;
		float x2 = w2.x - w0.x;
		float y1 = w1.y - w0.y;
		float y2 = w2.y - w0.y;

		float r = 1.0f / (x1 * y2 - x2 * y1);
		Vector t = (e1 * y2 - e2 * y1) * r;
		Vector b = (e2 * x1 - e1 * x2) * r;

		Tangent[i0] += t;
		Tangent[i1] += t;
		Tangent[i2] += t;
		Bitangent[i0] += b;
		Bitangent[i1] += b;
		Bitangent[i2] += b;
	}

	// 각각의 Tangent를 Orthonormalize 하고, Handedness 하게 계산가능하도록 w 에 부호 추가
	for (int32 i = 0; i < InVertexCount; ++i)
	{
		const Vector& t = Tangent[i];
		const Vector& b = Bitangent[i];
		const Vector& n = InNormalArray[i];

		auto Reject = [](const Vector& t, const Vector& n) {
			return (t - t.DotProduct(n) * n).GetNormalize();
		};

		OutTangentArray[i] = Vector4(Reject(t, n), 0.0f); // Normalize(t - Dot(t, n)*n)
		OutTangentArray[i].w = (t.CrossProduct(b).DotProduct(n) > 0.0f) ? 1.0f : -1.0f;
	}

	delete[] Tangent;
}

void jQuadPrimitive::SetPlane(const jPlane& plane)
{
	Plane = plane;
	RenderObjects[0]->SetRot(plane.n.GetEulerAngleFrom());
	RenderObjects[0]->SetPos(plane.n * plane.d);
}

void jBillboardQuadPrimitive::Update(float deltaTime)
{
	if (Camera)
	{
		const Vector normalizedCameraDir = (Camera->Pos - RenderObjects[0]->GetPos()).GetNormalize();
		const Vector eularAngleOfCameraDir = normalizedCameraDir.GetEulerAngleFrom();

		RenderObjects[0]->SetRot(eularAngleOfCameraDir);
	}
	else
	{
		JMESSAGE("BillboardQuad is updated without camera");
	}
}

//void jUIQuadPrimitive::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	SetUniformParams(shader);
//	__super::Draw(InRenderFrameContext, camera, shader, lights, instanceCount);
//}

void jUIQuadPrimitive::SetTexture(const jTexture* texture)
{
	//if (RenderObject->MaterialData.Params.size() > 0)
	//	RenderObject->MaterialData.SetMaterialParam(0, texture);
	//else
	//	RenderObject->MaterialData.AddMaterialParam(GetCommonTextureName(0), texture);
	// todo
	check(0);
}

void jUIQuadPrimitive::SetUniformParams(const jShader* shader) const
{
	//g_rhi->SetShader(shader);
	//SET_UNIFORM_BUFFER_STATIC("PixelSize", Vector2(1.0f / SCR_WIDTH, 1.0f / SCR_HEIGHT), shader);
	//SET_UNIFORM_BUFFER_STATIC("POSITION0", Pos, shader);
	//SET_UNIFORM_BUFFER_STATIC("Size", Size, shader);

	// Todo
	check(0);
}

const jTexture* jUIQuadPrimitive::GetTexture() const
{
	//if (RenderObject && (RenderObject->MaterialData.Params.size() > 0))
	//	return RenderObject->MaterialData.Params[0].Texture;

	// Todo
	check(0);
	return nullptr;
}

//void jFullscreenQuadPrimitive::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	SetUniformBuffer(shader);
//	__super::Draw(InRenderFrameContext, camera, shader, lights, instanceCount);
//}

void jFullscreenQuadPrimitive::SetUniformBuffer(const jShader* shader) const
{
	//g_rhi->SetShader(shader);
	//SET_UNIFORM_BUFFER_STATIC("PixelSize", Vector2(1.0f / SCR_WIDTH, 1.0f / SCR_HEIGHT), shader);

	// Todo
	check(0);
}

void jFullscreenQuadPrimitive::SetTexture(int index, const jTexture* texture, const jSamplerStateInfo* samplerState)
{
	// Todo
	check(0);
	//RenderObject->MaterialData.Params.resize(index + 1);
	//RenderObject->MaterialData.SetMaterialParam(index, GetCommonTextureName(index), texture, samplerState);
}

void jBoundBoxObject::SetUniformBuffer(const jShader* shader)
{
	// g_rhi->SetShader(shader);
	// SET_UNIFORM_BUFFER_STATIC("Color", Color, shader);

	// Todo
	check(0);
}
void jBoundBoxObject::UpdateBoundBox(const jBoundBox& boundBox)
{
	BoundBox = boundBox;
	UpdateBoundBox();
}

void jBoundBoxObject::UpdateBoundBox()
{
	float vertices[] = {
		// 아래
		BoundBox.Min.x, BoundBox.Min.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Min.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Min.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Min.y, BoundBox.Max.z,
		BoundBox.Max.x, BoundBox.Min.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Min.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Min.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Min.y, BoundBox.Min.z,

		// 위
		BoundBox.Min.x, BoundBox.Max.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Max.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Max.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Max.y, BoundBox.Max.z,
		BoundBox.Max.x, BoundBox.Max.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Max.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Max.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Max.y, BoundBox.Min.z,

		// 옆
		BoundBox.Min.x, BoundBox.Min.y, BoundBox.Min.z,
		BoundBox.Min.x, BoundBox.Max.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Min.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Max.y, BoundBox.Min.z,
		BoundBox.Max.x, BoundBox.Min.y, BoundBox.Max.z,
		BoundBox.Max.x, BoundBox.Max.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Max.y, BoundBox.Max.z,
		BoundBox.Min.x, BoundBox.Min.y, BoundBox.Max.z,
	};

	const int32 elementCount = static_cast<int32>(_countof(vertices) / 3);
	JASSERT(RenderObjectGeometryDataPtr->VertexStream->ElementCount == elementCount);

	jStreamParam<float>* PositionParam = static_cast<jStreamParam<float>*>(RenderObjectGeometryDataPtr->VertexStream->Params[0].get());
	memcpy(&PositionParam->Data[0], vertices, sizeof(vertices));

	RenderObjectGeometryDataPtr->UpdateVertexStream();
}

void jBoundSphereObject::SetUniformBuffer(const jShader* shader)
{
	// g_rhi->SetShader(shader);
	// SET_UNIFORM_BUFFER_STATIC("Color", Color, shader);

	// Todo
	check(0);
}

void jArrowSegmentPrimitive::Update(float deltaTime)
{
	__super::Update(deltaTime);

	if (SegmentObject)
		SegmentObject->Update(deltaTime);
	if (ConeObject)
		ConeObject->Update(deltaTime);
}

//void jArrowSegmentPrimitive::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	__super::Draw(InRenderFrameContext, camera, shader, lights);
//
//	if (SegmentObject)
//		SegmentObject->Draw(InRenderFrameContext, camera, shader, lights);
//	if (ConeObject)
//		ConeObject->Draw(InRenderFrameContext, camera, shader, lights);
//}


void jArrowSegmentPrimitive::SetPos(const Vector& pos)
{
	SegmentObject->RenderObjects[0]->SetPos(pos);
}

void jArrowSegmentPrimitive::SetStart(const Vector& start)
{
	SegmentObject->Start = start;
}

void jArrowSegmentPrimitive::SetEnd(const Vector& end)
{
	SegmentObject->End = end;
}

void jArrowSegmentPrimitive::SetTime(float time)
{
	SegmentObject->Time = time;
}

//////////////////////////////////////////////////////////////////////////
// Utilities
namespace jPrimitiveUtil
{

std::vector<float> GenerateColor(const Vector4& color, int32 elementCount)
{
	std::vector<float> temp;
	temp.resize(elementCount * 4);
	for (int i = 0; i < elementCount; ++i)
	{
		temp[i * 4 + 0] = color.x;
		temp[i * 4 + 1] = color.y;
		temp[i * 4 + 2] = color.z;
		temp[i * 4 + 3] = color.w;
	}

	return std::move(temp);
}

jBoundBox GenerateBoundBox(const std::vector<float>& vertices)
{
	auto min = Vector(FLT_MAX);
	auto max = Vector(FLT_MIN);
	for (size_t i = 0; i < vertices.size() / 3; ++i)
	{
		auto curIndex = i * 3;
		auto x = vertices[curIndex];
		auto y = vertices[curIndex + 1];
		auto z = vertices[curIndex + 2];
		if (max.x < x)
			max.x = x;
		if (max.y < y)
			max.y = y;
		if (max.z < z)
			max.z = z;

		if (min.x > x)
			min.x = x;
		if (min.y > y)
			min.y = y;
		if (min.z > z)
			min.z = z;
	}

	return { min, max };
}

jBoundSphere GenerateBoundSphere(const std::vector<float>& vertices)
{
	auto maxDist = FLT_MIN;
	for (size_t i = 0; i < vertices.size() / 3; ++i)
	{
		auto curIndex = i * 3;
		auto x = vertices[curIndex];
		auto y = vertices[curIndex + 1];
		auto z = vertices[curIndex + 2];

		auto currentPos = Vector(x, y, z);
		const auto dist = currentPos.Length();
		if (maxDist < dist)
			maxDist = dist;
	}
	return { maxDist };
}

void CreateBoundObjects(const std::vector<float>& vertices, jObject* ownerObject)
{
	ownerObject->BoundBox = GenerateBoundBox(vertices);
	ownerObject->BoundSphere = GenerateBoundSphere(vertices);

	ownerObject->BoundBoxObject = CreateBoundBox(ownerObject->BoundBox, ownerObject);
	ownerObject->BoundSphereObject = CreateBoundSphere(ownerObject->BoundSphere, ownerObject);
	jObject::AddBoundBoxObject(ownerObject->BoundBoxObject);
	jObject::AddBoundSphereObject(ownerObject->BoundSphereObject);
}

jBoundBoxObject* CreateBoundBox(jBoundBox boundBox, jObject* ownerObject, const Vector4& color)
{
	float vertices[] = {
		// 아래
		boundBox.Min.x, boundBox.Min.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Min.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Min.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Min.y, boundBox.Max.z,
		boundBox.Max.x, boundBox.Min.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Min.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Min.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Min.y, boundBox.Min.z,

		// 위
		boundBox.Min.x, boundBox.Max.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Max.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Max.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Max.y, boundBox.Max.z,
		boundBox.Max.x, boundBox.Max.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Max.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Max.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Max.y, boundBox.Min.z,

		// 옆
		boundBox.Min.x, boundBox.Min.y, boundBox.Min.z,
		boundBox.Min.x, boundBox.Max.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Min.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Max.y, boundBox.Min.z,
		boundBox.Max.x, boundBox.Min.y, boundBox.Max.z,
		boundBox.Max.x, boundBox.Max.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Max.y, boundBox.Max.z,
		boundBox.Min.x, boundBox.Min.y, boundBox.Max.z,
	};

	const int32 elementCount = static_cast<int32>(_countof(vertices) / 3);

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], vertices, sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::LINES;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jBoundBoxObject();
	auto renderObject = new jRenderObject();
	object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
	renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
	object->RenderObjects.push_back(renderObject);
	object->SkipShadowMapGen = true;
	object->SkipUpdateShadowVolume = true;
	object->OwnerObject = ownerObject;
	object->Color = color;

	if (object->OwnerObject)
	{
		object->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
		{
			JASSERT(thisObject);
			auto boundBoxObject = static_cast<jBoundBoxObject*>(thisObject);
			if (!boundBoxObject)
				return;

			JASSERT(boundBoxObject->OwnerObject);
			auto ownerObject = boundBoxObject->OwnerObject;

			boundBoxObject->RenderObjects[0]->SetPos(ownerObject->RenderObjects[0]->GetPos());
			boundBoxObject->RenderObjects[0]->SetRot(ownerObject->RenderObjects[0]->GetRot());
			boundBoxObject->RenderObjects[0]->SetScale(ownerObject->RenderObjects[0]->GetScale());
			boundBoxObject->Visible = ownerObject->Visible;
		};
	}

	return object;
}

jBoundSphereObject* CreateBoundSphere(jBoundSphere boundSphere, jObject* ownerObject, const Vector4& color)
{
	int32 slice = 15;
	if (slice % 2)
		++slice;

	std::vector<float> vertices;
	const float stepRadian = DegreeToRadian(360.0f / slice);
	const float radius = boundSphere.Radius;

	for (int32 j = 0; j < slice / 2; ++j)
	{
		for (int32 i = 0; i <= slice; ++i)
		{
			const Vector temp(cosf(stepRadian * i) * radius * sinf(stepRadian * (j + 1))
				, cosf(stepRadian * (j + 1)) * radius
				, sinf(stepRadian * i) * radius * sinf(stepRadian * (j + 1)));
			vertices.push_back(temp.x);
			vertices.push_back(temp.y);
			vertices.push_back(temp.z);
		}
	}

	// top
	vertices.push_back(0.0f);
	vertices.push_back(radius);
	vertices.push_back(0.0f);

	// down
	vertices.push_back(0.0f);
	vertices.push_back(-radius);
	vertices.push_back(0.0f);

	int32 elementCount = static_cast<int32>(vertices.size() / 3);

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(vertices.size());
		memcpy(&streamParam->Data[0], &vertices[0], vertices.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::LINES;
	vertexStreamData->ElementCount = elementCount;

	// IndexStream 추가
	std::vector<uint32> faces;
	int32 iCount = 0;
	int32 toNextSlice = slice + 1;
	int32 temp = 6;
	for (int32 i = 0; i < (slice) / 2 - 2; ++i, iCount += 1)
	{
		for (int32 i = 0; i < slice; ++i, iCount += 1)
		{
			faces.push_back(iCount); faces.push_back(iCount + 1); faces.push_back(iCount + toNextSlice);
			faces.push_back(iCount + toNextSlice); faces.push_back(iCount + 1); faces.push_back(iCount + toNextSlice + 1);
		}
	}

	for (int32 i = 0; i < slice; ++i, iCount += 1)
	{
		faces.push_back(iCount);
		faces.push_back(iCount + 1);
		faces.push_back(elementCount - 1);
	}

	iCount = 0;
	for (int32 i = 0; i < slice; ++i, iCount += 1)
	{
		faces.push_back(iCount);
		faces.push_back(elementCount - 2);
		faces.push_back(iCount + 1);
	}

	auto indexStreamData = std::make_shared<jIndexStreamData>();
	indexStreamData->ElementCount = static_cast<int32>(faces.size());
	{
		auto streamParam = new jStreamParam<uint32>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT32, sizeof(uint32) * 3));
		streamParam->Stride = sizeof(uint32) * 3;
		streamParam->Name = jName("Index");
		streamParam->Data.resize(faces.size());
		memcpy(&streamParam->Data[0], &faces[0], faces.size() * sizeof(uint32));
		indexStreamData->Param = streamParam;
	}

	auto object = new jBoundSphereObject();
	auto renderObject = new jRenderObject();
	object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, indexStreamData);
	renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
	object->RenderObjects.push_back(renderObject);
	object->SkipShadowMapGen = true;
	object->SkipUpdateShadowVolume = true;
	object->OwnerObject = ownerObject;
	object->BoundSphere = boundSphere;

	object->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		JASSERT(thisObject);
		auto boundSphereObject = static_cast<jBoundSphereObject*>(thisObject);
		if (!boundSphereObject)
			return;

		JASSERT(boundSphereObject->OwnerObject);
		auto ownerObject = boundSphereObject->OwnerObject;

		boundSphereObject->RenderObjects[0]->SetPos(ownerObject->RenderObjects[0]->GetPos());
		boundSphereObject->RenderObjects[0]->SetRot(ownerObject->RenderObjects[0]->GetRot());
		boundSphereObject->RenderObjects[0]->SetScale(ownerObject->RenderObjects[0]->GetScale());
		boundSphereObject->Visible = ownerObject->Visible;
	};

	return object;
}

//////////////////////////////////////////////////////////////////////////
// Primitive Generation

jRenderObject* CreateQuad_Internal(const Vector& pos, const Vector& size, const Vector& scale, const Vector4& color)
{
	auto halfSize = size / 2.0;
	auto offset = Vector::ZeroVector;

	float vertices[] = {
        offset.x + (halfSize.x),	0.0f,	offset.z + -(-halfSize.z),
		offset.x + (-halfSize.x),	0.0f,	offset.z + -(-halfSize.z),
		offset.x + (halfSize.x),	0.0f,	offset.z + -(halfSize.z),
        offset.x + (-halfSize.x),	0.0f,	offset.z + -(halfSize.z),
		offset.x + (halfSize.x),	0.0f,	offset.z + -(halfSize.z),
        offset.x + (-halfSize.x),	0.0f,	offset.z + -(-halfSize.z),
	};

	float normals[] = {
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
	};

	Vector2 texcoords[] = {
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 0.0f),
		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 0.0f),
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 1.0f),
	};

	const int32 elementCount = _countof(vertices) / 3;

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], &vertices[0], sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], normals, sizeof(normals));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		// Create tangent
		const int32 TotalNumOfTriangles = elementCount / 3;
		std::vector<Triangle> TriangleArray;
		for (int32 i = 0; i < TotalNumOfTriangles; ++i)
		{
			TriangleArray.push_back(Triangle{ i * 3, i * 3 + 1, i * 3 + 2 });
		}

		constexpr int32 veticesElement = sizeof(vertices) / sizeof(float);
		Vector4 TangentArray[elementCount];
		CalculateTangents(&TangentArray[0], (int32)TriangleArray.size(), &TriangleArray[0], elementCount
			, (const Vector*)(vertices), (const Vector*)&normals[0], &texcoords[0]);

		std::vector<Vector> tangents;
		for (int32 i = 0; i < elementCount; ++i)
		{
			tangents.push_back(Vector(TangentArray[i].x, TangentArray[i].y, TangentArray[i].z));
		}

		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], &tangents[0], tangents.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(elementCount * 2);
		memcpy(&streamParam->Data[0], texcoords, sizeof(texcoords));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	auto renderObject = new jRenderObject();
    renderObject->CreateRenderObject(std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr));
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	return renderObject;
}

jQuadPrimitive* CreateQuad(const Vector& pos, const Vector& size, const Vector& scale, const Vector4& color)
{
	auto object = new jQuadPrimitive();
	auto RenderObject = CreateQuad_Internal(pos, size, scale, color);
	object->RenderObjectGeometryDataPtr = RenderObject->GeometryDataPtr;
	object->RenderObjects.push_back(RenderObject);
	RenderObject->IsTwoSided = true;
	// object->CreateBoundBox();
	return object;
}

//////////////////////////////////////////////////////////////////////////
jObject* CreateGizmo(const Vector& pos, const Vector& rot, const Vector& scale)
{
	float length = 5.0f;
	float length2 = length * 0.6f;
	float vertices[] = {
		0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, length,
		0.0f, 0.0f, length,
		length2 / 2.0f, 0.0f, length2,
		0.0f, 0.0f, length,
		-length2 / 2.0f, 0.0f, length2,

		0.0f, 0.0f, 0.0f,
		length, 0.0f, 0.0f,
		length, 0.0f, 0.0f,
		length2, 0.0f, length2 / 2.0f,
		length, 0.0f, 0.0f,
		length2, 0.0f, -length2 / 2.0f,

		0.0f, 0.0f, 0.0f,
		0.0f, length, 0.0f,
		0.0f, length, 0.0f,
		length2 / 2.0f, length2, 0.0f,
		0.0f, length, 0.0f,
		-length2 / 2.0f, length2, 0.0f,
	};

	float colors[] = {
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		1.0f, 0.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
		0.0f, 1.0f, 0.0f, 1.0f,
	};

	const int32 elementCount = _countof(vertices) / 3;

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], vertices, sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data.resize(elementCount * 4);
		memcpy(&streamParam->Data[0], colors, sizeof(colors));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(elementCount * 3);
		for (int32 i = 0; i < elementCount; ++i)
		{
			streamParam->Data[i * 3 + 0] = 0.0f;
			streamParam->Data[i * 3 + 1] = 1.0f;
			streamParam->Data[i * 3 + 2] = 0.0f;
		}
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(elementCount * 3);
		for (int32 i = 0; i < elementCount; ++i)
		{
			streamParam->Data[i * 3 + 0] = 1.0f;
			streamParam->Data[i * 3 + 1] = 0.0f;
			streamParam->Data[i * 3 + 2] = 0.0f;
		}
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(elementCount * 2, 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::LINES;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jQuadPrimitive();
	auto renderObject = new jRenderObject();
	object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
	renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
	object->RenderObjects.push_back(renderObject);
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	// object->CreateBoundBox();
	return object;
}

jObject* CreateTriangle(const Vector& pos, const Vector& size, const Vector& scale, const Vector4& color)
{
	const auto halfSize = size / 2.0f;
	const auto offset = Vector::ZeroVector;

	float vertices[] = {
		offset.x + (halfSize.x), 0.0, offset.z + (-halfSize.z),
		offset.x + (halfSize.x), 0.0, offset.z + (halfSize.z),
		offset.x + (-halfSize.x), 0.0, offset.z + (-halfSize.z),
	};

	Vector2 texcoords[] = {
		Vector2(0.0f, 0.0f),
		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 1.0f),
	};

	int32 elementCount = _countof(vertices) / 3;

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], vertices, sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(elementCount * 3);
		for (int32 i = 0; i < elementCount; ++i)
		{
			streamParam->Data[i * 3 + 0] = 0.0f;
			streamParam->Data[i * 3 + 1] = 1.0f;
			streamParam->Data[i * 3 + 2] = 0.0f;
		}
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(elementCount * 3);
		for (int32 i = 0; i < elementCount; ++i)
		{
			streamParam->Data[i * 3 + 0] = 1.0f;
			streamParam->Data[i * 3 + 1] = 0.0f;
			streamParam->Data[i * 3 + 2] = 0.0f;
		}
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(elementCount * 2);
		memcpy(&streamParam->Data[0], texcoords, sizeof(texcoords));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jObject();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	renderObject->IsTwoSided = true;
	// object->CreateBoundBox();

	return object;
}

jObject* CreateCube(const Vector& pos, const Vector& size, const Vector& scale, const Vector4& color)
{
	const Vector halfSize = size / 2.0f;
	const Vector offset = Vector::ZeroVector;

	float vertices[] = {
		offset.x + (halfSize.x),  offset.y + (-halfSize.y),    offset.z + (halfSize.z),
		offset.x + (-halfSize.x),  offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (halfSize.z),		
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (halfSize.x),  offset.y + (-halfSize.y),     offset.z + (halfSize.z),
		offset.x + (halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),

		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),

		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),

		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),
		offset.x + (halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (halfSize.x),   offset.y + (-halfSize.y),     offset.z + (halfSize.z),
		offset.x + (halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),
		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),

		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),
		offset.x + (halfSize.x),   offset.y + (-halfSize.y),     offset.z + (halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (halfSize.z),
		offset.x + (halfSize.x),   offset.y + (-halfSize.y),     offset.z + (halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),
		offset.x + (halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),

		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (-halfSize.y),     offset.z + (halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (-halfSize.z),
		offset.x + (-halfSize.x),   offset.y + (halfSize.y),     offset.z + (halfSize.z),
	};

	float normals[] = {
		0.0f, -1.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		0.0f, -1.0f, 0.0f,

		0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f,

		0.0f, 0.0f, -1.0f,
		0.0f, 0.0f, -1.0f,
		0.0f, 0.0f, -1.0f,
		0.0f, 0.0f, -1.0f,
		0.0f, 0.0f, -1.0f,
		0.0f, 0.0f, -1.0f,

		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,

		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 1.0f,

		-1.0f, 0.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
		-1.0f, 0.0f, 0.0f,
	};

	Vector2 texcoords[] = {
		Vector2(0.0f, 0.0f),
		Vector2(1.0f, 1.0f),
		Vector2(1.0f, 0.0f),
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 0.0f),
		Vector2(0.0f, 1.0f),

		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 0.0f),

		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 0.0f),

		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 0.0f),

		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 0.0f),

		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 0.0f),
		Vector2(0.0f, 0.0f),
	};

	const int32 elementCount = _countof(vertices) / 3;

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], vertices, sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], normals, sizeof(normals));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		// Create tangent
		const int32 TotalNumOfTriangles = elementCount / 3;
		std::vector<Triangle> TriangleArray;
		for (int32 i = 0; i < TotalNumOfTriangles; ++i)
		{
			TriangleArray.push_back(Triangle{ i * 3, i * 3 + 1, i * 3 + 2 });
		}

		constexpr int32 veticesElement = sizeof(vertices) / sizeof(float);
		Vector4 TangentArray[elementCount];
		CalculateTangents(&TangentArray[0], (int32)TriangleArray.size(), &TriangleArray[0], elementCount
			, (const Vector*)(vertices), (const Vector*)&normals[0], &texcoords[0]);

		std::vector<Vector> tangents;
		for (int32 i = 0; i < elementCount; ++i)
		{
			tangents.push_back(Vector(TangentArray[i].x, TangentArray[i].y, TangentArray[i].z));
		}

		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(elementCount * 3);
		memcpy(&streamParam->Data[0], &tangents[0], tangents.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(elementCount * 2);
		memcpy(&streamParam->Data[0], texcoords, sizeof(texcoords));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jObject();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	// object->CreateBoundBox();

	return object;
}

jObject* CreateCapsule(const Vector& pos, float height, float radius, int32 slice, const Vector& scale, const Vector4& color)
{
	if (height < 0)
	{
		height = 0.0f;
		JASSERT(!"capsule height must be more than or equal zero.");
		return nullptr;
	}

	const float stepRadian = DegreeToRadian(360.0f / slice);
	const int32 halfSlice = slice / 2;

	const float halfHeight = height / 2.0f;
	std::vector<float> vertices;
	vertices.reserve((halfSlice + 1) * (slice + 1) * 3);
	std::vector<float> normals;
	normals.reserve((halfSlice + 1) * (slice + 1) * 3);

	if (slice % 2)
		++slice;

	for (int32 j = 0; j <= halfSlice; ++j)
	{
		const bool isUpperSphere = (j > halfSlice / 2.0f);
		const bool isLowerSphere = (j < halfSlice / 2.0f);

		for (int32 i = 0; i <= slice; ++i)
		{
			float x = cosf(stepRadian * i) * radius * sinf(stepRadian * j);
			float y = cosf(stepRadian * j) * radius;
			float z = sinf(stepRadian * i) * radius * sinf(stepRadian * j);
			float yExt = 0.0f;
			if (isUpperSphere)
				yExt = -halfHeight;
			if (isLowerSphere)
				yExt = halfHeight;
			vertices.push_back(x);
			vertices.push_back(y + yExt);
			vertices.push_back(z);

			Vector normal;
			if (!isUpperSphere && !isLowerSphere)
				normal = Vector(x, 0.0, z).GetNormalize();
			else
				normal = Vector(x, y, z).GetNormalize();
			normals.push_back(normal.x);
			normals.push_back(normal.y);
			normals.push_back(normal.z);
		}
	}

	int32 elementCount = static_cast<int32>(vertices.size() / 3);

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(vertices.size());
		memcpy(&streamParam->Data[0], &vertices[0], vertices.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(normals.size());
		memcpy(&streamParam->Data[0], &normals[0], normals.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	// IndexStream 추가

	std::vector<uint32> faces;
	faces.reserve((halfSlice + 1) * (slice - 1) * 6);
	int32 iCount = 0;
	int32 toNextSlice = slice + 1;
	for (int32 j = 0; j <= halfSlice; ++j)
	{
		for (int32 i = 0; i < (slice - 1); ++i, iCount += 1)
		{
			faces.push_back(iCount + 1); faces.push_back(iCount); faces.push_back(iCount + toNextSlice);
			faces.push_back(iCount + 1); faces.push_back(iCount + toNextSlice); faces.push_back(iCount + toNextSlice + 1);
		}
	}

	auto indexStreamData = std::make_shared<jIndexStreamData>();
	indexStreamData->ElementCount = static_cast<int32>(faces.size());
	{
		auto streamParam = new jStreamParam<uint32>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT32, sizeof(uint32) * 3));
		streamParam->Stride = sizeof(uint32) * 3;
		streamParam->Name = jName("Index");
		streamParam->Data.resize(faces.size());
		memcpy(&streamParam->Data[0], &faces[0], faces.size() * sizeof(uint32));
		indexStreamData->Param = streamParam;
	}

	auto object = new jObject();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, indexStreamData);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	// object->CreateBoundBox();

	return object;
}

jConePrimitive* CreateCone(const Vector& pos, float height, float radius, int32 slice, const Vector& scale, const Vector4& color, bool isWireframe /*= false*/, bool createBoundInfo/* = true*/)
{
	const float halfHeight = height / 2.0f;
	const Vector topVert(0.0f, halfHeight, 0.0f);
	const Vector bottomVert(0.0f, -halfHeight, 0.0f);

	if (slice % 2)
		++slice;

	std::vector<float> vertices(slice * 18);
	float stepRadian = DegreeToRadian(360.0f / slice);
	for (int32 i = 1; i <= slice; ++i)
	{
		const float rad = i * stepRadian;
		const float prevRad = rad - stepRadian;

		Vector* currentPos = (Vector*)&vertices[(i - 1) * 18];
		// Top
		*currentPos = Vector(cosf(rad) * radius,		bottomVert.y,		sinf(rad) * radius);		++currentPos;
		*currentPos = Vector(topVert);	++currentPos;
		*currentPos = Vector(cosf(prevRad) * radius,	bottomVert.y,		sinf(prevRad) * radius);	++currentPos;

		// Bottom
		*currentPos = Vector(cosf(prevRad) * radius,	bottomVert.y,		sinf(prevRad) * radius);	++currentPos;
		*currentPos = Vector(bottomVert);	++currentPos;
		*currentPos = Vector(cosf(rad) * radius,		bottomVert.y,		sinf(rad) * radius);		++currentPos;
	}

	int32 elementCount = static_cast<int32>(vertices.size() / 3);

	std::vector<float> normals(slice * 18);

	// https://stackoverflow.com/questions/51015286/how-can-i-calculate-the-normal-of-a-cone-face-in-opengl-4-5
	// lenght of the flank of the cone
	const float flank_len = sqrtf(radius * radius + height * height);

	// unit vector along the flank of the cone
	const float cone_x = radius / flank_len;
	const float cone_y = -height / flank_len;

	// Cone Top Normal
	for (int32 i = 1; i <= slice; ++i)
	{
		const float rad = i * stepRadian;
		const Vector normal(-cone_y * cosf(rad), cone_x, -cone_y * sinf(rad));

		float* currentPos = &normals[(i - 1) * 18];

		// Top
		memcpy(&currentPos[0], &normal, sizeof(normal));
		memcpy(&currentPos[3], &normal, sizeof(normal));
		memcpy(&currentPos[6], &normal, sizeof(normal));

		// Bottom
		const Vector temp(0.0f, -1.0f, 0.0f);
		memcpy(&currentPos[9], &temp, sizeof(temp));
		memcpy(&currentPos[12], &temp, sizeof(temp));
		memcpy(&currentPos[15], &temp, sizeof(temp));
	}

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(vertices.size());
		memcpy(&streamParam->Data[0], &vertices[0], vertices.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(normals.size());
		memcpy(&streamParam->Data[0], &normals[0], normals.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = isWireframe ? EPrimitiveType::LINES : EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jConePrimitive();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	object->Height = height;
	object->Radius = radius;
	object->Color = color;
	//if (createBoundInfo)
	//	object->CreateBoundBox();
	return object;
}

jObject* CreateCylinder(const Vector& pos, float height, float radius, int32 slice, const Vector& scale, const Vector4& color)
{
	const auto halfHeight = height / 2.0f;
	const Vector topVert(0.0f, halfHeight, 0.0f);
	const Vector bottomVert(0.0f, -halfHeight, 0.0f);

	if (slice % 2)
		++slice;

	std::vector<float> vertices(slice * 36);

	const float stepRadian = DegreeToRadian(360.0f / slice);
	for (int32 i = 0; i < slice; ++i)
	{
		float rad = i * stepRadian;
		float prevRad = rad - stepRadian;

		Vector* currentPos = (Vector*)&vertices[i * 36];

		// Top
		*currentPos = Vector(topVert); ++currentPos;
		*currentPos = Vector(cosf(prevRad) * radius,	topVert.y,		sinf(prevRad) * radius);	++currentPos;
		*currentPos = Vector(cosf(rad) * radius,		topVert.y,		sinf(rad) * radius);		++currentPos;

		// Mid
		*currentPos = Vector(cosf(prevRad) * radius,	topVert.y,		sinf(prevRad) * radius);	++currentPos;
		*currentPos = Vector(cosf(prevRad) * radius,	bottomVert.y,	sinf(prevRad) * radius);	++currentPos;
		*currentPos = Vector(cosf(rad) * radius,		topVert.y,		sinf(rad) * radius);		++currentPos;

		*currentPos = Vector(cosf(prevRad) * radius,	bottomVert.y,	sinf(prevRad) * radius);	++currentPos;
		*currentPos = Vector(cosf(rad) * radius,		bottomVert.y,	sinf(rad) * radius);		++currentPos;
		*currentPos = Vector(cosf(rad) * radius,		topVert.y,		sinf(rad) * radius);		++currentPos;

		// Bottom
		*currentPos = Vector(bottomVert.x,				bottomVert.y,	bottomVert.z);				++currentPos;
		*currentPos = Vector(cosf(rad) * radius,		bottomVert.y,	sinf(rad) * radius);		++currentPos;
		*currentPos = Vector(cosf(prevRad) * radius,	bottomVert.y,	sinf(prevRad) * radius);	++currentPos;
	}

	int32 elementCount = static_cast<int32>(vertices.size() / 3);

	std::vector<float> normals(slice * 36);

	// https://stackoverflow.com/questions/51015286/how-can-i-calculate-the-normal-of-a-cone-face-in-opengl-4-5
	// lenght of the flank of the cone
	const float flank_len = sqrtf(radius * radius + height * height);

	// unit vector along the flank of the cone
	const float cone_x = radius / flank_len;
	const float cone_y = -height / flank_len;

	// Cone Top Normal
	for (int32 i = 0; i < slice; ++i)
	{
		const float rad = i * stepRadian;
		const float prevRad = rad - stepRadian;

		Vector* currentPos = (Vector*)&normals[i * 36];

		// Top
		const Vector temp(0.0f, 1.0f, 0.0f);
		*currentPos = Vector(temp); ++currentPos;
		*currentPos = Vector(temp); ++currentPos;
		*currentPos = Vector(temp); ++currentPos;

		// Mid
		*currentPos = Vector(cosf(prevRad),		0.0f,		sinf(prevRad));			++currentPos;
		*currentPos = Vector(cosf(prevRad),		0.0f,		sinf(prevRad));			++currentPos;
		*currentPos = Vector(cosf(rad),			0.0f,		sinf(rad));				++currentPos;

		*currentPos = Vector(cosf(prevRad),		0.0f,		sinf(prevRad));			++currentPos;
		*currentPos = Vector(cosf(rad),			0.0f,		sinf(rad));				++currentPos;
		*currentPos = Vector(cosf(rad),			0.0f,		sinf(rad));				++currentPos;

		// Bottom
		const Vector temp2(0.0f, -1.0f, 0.0f);
        *currentPos = Vector(temp2); ++currentPos;
        *currentPos = Vector(temp2); ++currentPos;
        *currentPos = Vector(temp2); ++currentPos;
	}
	/////////////////////////////////////////////////////

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(vertices.size());
		memcpy(&streamParam->Data[0], &vertices[0], vertices.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(normals.size());
		memcpy(&streamParam->Data[0], &normals[0], normals.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jCylinderPrimitive();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	object->Height = height;
	object->Radius = radius;
	object->Color = color;
	// object->CreateBoundBox();
	return object;
}

jObject* CreateSphere(const Vector& pos, float radius, int32 slice, const Vector& scale, const Vector4& color, bool isWireframe /*= false*/, bool createBoundInfo/* = true*/)
{
	const auto offset = Vector::ZeroVector;

	if (slice < 6)
		slice = 6;
	else if (slice % 2)
		++slice;

	const float stepRadian = DegreeToRadian(360.0f / slice);

	std::vector<float> vertices;
	int j = 0;

	for (int32 j = 0; j < slice / 2; ++j)
	{
		for (int32 i = 0; i <= slice; ++i)
		{
			const Vector temp(offset.x + cosf(stepRadian * i) * radius * sinf(stepRadian * (j + 1))
							, offset.z + cosf(stepRadian * (j + 1)) * radius
							, offset.y + sinf(stepRadian * i) * radius * sinf(stepRadian * (j + 1)));
			vertices.push_back(temp.x);
			vertices.push_back(temp.y);
			vertices.push_back(temp.z);
		}
	}

	// top
	vertices.push_back(0.0f);
	vertices.push_back(radius);
	vertices.push_back(0.0f);

	// down
	vertices.push_back(0.0f);
	vertices.push_back(-radius);
	vertices.push_back(0.0f);

	int32 elementCount = static_cast<int32>(vertices.size() / 3);

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(vertices.size());
		memcpy(&streamParam->Data[0], &vertices[0], vertices.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	std::vector<float> normals(vertices.size());
	for (int i = 0; i < normals.size() / 3; ++i)
	{
		const int32 curIndex = i * 3;
		const Vector normal = Vector(vertices[curIndex], vertices[curIndex + 1], vertices[curIndex + 2]).GetNormalize();
		memcpy(&normals[curIndex], &normal, sizeof(normal));
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("NORMAL");
		streamParam->Data.resize(normals.size());
		memcpy(&streamParam->Data[0], &normals[0], normals.size() * sizeof(float));
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("TANGENT");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	// Dummy
	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("TEXCOORD");
		streamParam->Data.resize(normals.size(), 0.0f);
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = isWireframe ? EPrimitiveType::LINES : EPrimitiveType::TRIANGLES;
	vertexStreamData->ElementCount = elementCount;

	// IndexStream 추가
	std::vector<uint32> faces;
	int32 iCount = 0;
	int32 toNextSlice = slice + 1;
	int32 temp = 6;
	for (int32 i = 0; i < (slice) / 2 - 2; ++i, iCount += 1)
	{
		for (int32 i = 0; i < slice; ++i, iCount += 1)
		{
			faces.push_back(iCount + 1); faces.push_back(iCount); faces.push_back(iCount + toNextSlice);
			faces.push_back(iCount + 1); faces.push_back(iCount + toNextSlice); faces.push_back(iCount + toNextSlice + 1);
		}
	}

	for (int32 i = 0; i < slice; ++i, iCount += 1)
	{
		faces.push_back(iCount + 1);
		faces.push_back(iCount);
		faces.push_back(elementCount - 1);
	}

	iCount = 0;
	for (int32 i = 0; i < slice; ++i, iCount += 1)
	{
		faces.push_back(elementCount - 2);
		faces.push_back(iCount);
		faces.push_back(iCount + 1);
	}

	auto indexStreamData = std::make_shared<jIndexStreamData>();
	indexStreamData->ElementCount = static_cast<int32>(faces.size());
	{
		auto streamParam = new jStreamParam<uint32>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::UINT32, sizeof(uint32) * 3));
		streamParam->Stride = sizeof(uint32) * 3;
		streamParam->Name = jName("Index");
		streamParam->Data.resize(faces.size());
		memcpy(&streamParam->Data[0], &faces[0], faces.size() * sizeof(uint32));
		indexStreamData->Param = streamParam;
	}

	auto object = new jObject();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, indexStreamData);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	renderObject->SetPos(pos);
	renderObject->SetScale(scale);
	//if (createBoundInfo)
	//	object->CreateBoundBox();
	return object;
}

jBillboardQuadPrimitive* CreateBillobardQuad(const Vector& pos, const Vector& size, const Vector& scale, const Vector4& color, jCamera* camera)
{
	auto object = new jBillboardQuadPrimitive();
    auto RenderObject = CreateQuad_Internal(pos, size, scale, color);
    object->RenderObjectGeometryDataPtr = RenderObject->GeometryDataPtr;
    object->RenderObjects.push_back(RenderObject);
	object->Camera = camera;
	RenderObject->IsTwoSided = true;
	return object;
}

jUIQuadPrimitive* CreateUIQuad(const Vector2& pos, const Vector2& size, jTexture* texture)
{
#if VULKAN_NDC_Y_FLIP
    float vertices[] = {
        0.0f, 0.0f,
		0.0f, 1.0f,
        1.0f, 0.0f,
		1.0f, 1.0f,
	};
#else
	float vertices[] = {
		0.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
	};
#endif

	int32 elementCount = _countof(vertices) / 2;

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 2));
		streamParam->Stride = sizeof(float) * 2;
		streamParam->Name = jName("VertPos");
		streamParam->Data.resize(_countof(vertices));
		memcpy(&streamParam->Data[0], &vertices[0], sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLE_STRIP;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jUIQuadPrimitive();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	object->Pos = pos;
	object->Size = size;

	return object;
}


jFullscreenQuadPrimitive* CreateFullscreenQuad(jTexture* texture)
{
#if VULKAN_NDC_Y_FLIP
	float vertices[] = { 0.0f, 2.0f, 1.0f };
#else
	float vertices[] = { 0.0f, 1.0f, 2.0f };
#endif

	uint32 elementCount = static_cast<uint32>(_countof(vertices));
	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float)));
		streamParam->Stride = sizeof(float);
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(_countof(vertices));
		memcpy(&streamParam->Data[0], &vertices[0], sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLE_STRIP;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jFullscreenQuadPrimitive();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	return object;
}

jSegmentPrimitive* CreateSegment(const Vector& start, const Vector& end, float time, const Vector4& color)
{
	Vector currentEnd(Vector::ZeroVector);
	if (time < 1.0)
	{
		float t = Clamp(time, 0.0f, 1.0f);
		currentEnd = (end - start) + start;
	}
	else
	{
		currentEnd = end;
	}

	float vertices[] = {
		start.x, start.y, start.z,
		currentEnd.x, currentEnd.y, currentEnd.z,
	};

	int32 elementCount = static_cast<int32>(_countof(vertices) / 3);

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::DYNAMIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(_countof(vertices));
		memcpy(&streamParam->Data[0], &vertices[0], sizeof(vertices));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(GenerateColor(color, elementCount));
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::LINES;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jSegmentPrimitive();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	renderObject = renderObject;
	renderObject->SetScale(Vector(time));
	object->Time = time;
	object->Start = start;
	object->End = end;
	object->Color = color;
	object->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		auto thisSegmentObject = static_cast<jSegmentPrimitive*>(thisObject);
		thisObject->RenderObjects[0]->SetScale(Vector(thisSegmentObject->Time));
	};
	// object->CreateBoundBox();
	return object;
}

jArrowSegmentPrimitive* CreateArrowSegment(const Vector& start, const Vector& end, float time, float coneHeight, float coneRadius, const Vector4& color)
{
	auto object = new jArrowSegmentPrimitive();
	object->SegmentObject = CreateSegment(start, end, time, color);
	object->ConeObject = CreateCone(Vector::ZeroVector, coneHeight, coneRadius, 10, Vector::OneVector, color);
	object->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		auto thisArrowSegmentObject = static_cast<jArrowSegmentPrimitive*>(thisObject);
		thisArrowSegmentObject->ConeObject->RenderObjects[0]->SetPos(thisArrowSegmentObject->SegmentObject->RenderObjects[0]->GetPos() + thisArrowSegmentObject->SegmentObject->GetCurrentEnd());
		thisArrowSegmentObject->ConeObject->RenderObjects[0]->SetRot(thisArrowSegmentObject->SegmentObject->GetDirectionNormalized().GetEulerAngleFrom());
	};

	return object;
}

jDirectionalLightPrimitive* CreateDirectionalLightDebug(const Vector& pos, const Vector& scale, float length, jCamera* targetCamera, jDirectionalLight* light, const char* textureFilename)
{
	jDirectionalLightPrimitive* object = new jDirectionalLightPrimitive();

    std::weak_ptr<jImageData> data = jImageFileLoader::GetInstance().LoadImageDataFromFile(jName(textureFilename), true);
    object->BillboardObject = jPrimitiveUtil::CreateBillobardQuad(pos, Vector::OneVector, scale, Vector4(1.0f), targetCamera);
    if (data.lock()->ImageData.size() > 0)
    {
        jTexture* texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName(textureFilename), true).lock().get();
		//object->BillboardObject->RenderObject->TextureSamplers.push_back({ .Texture = texture, .SamplerState = nullptr });
		object->BillboardObject->RenderObjects[0]->MaterialPtr = std::make_shared<jMaterial>();
		object->BillboardObject->RenderObjects[0]->MaterialPtr->TexData[static_cast<int32>(jMaterial::EMaterialTextureType::Albedo)].Texture = texture;
        object->BillboardObject->RenderObjects[0]->IsHiddenBoundBox = true;
    }
    object->ArrowSegementObject = jPrimitiveUtil::CreateArrowSegment(Vector::ZeroVector, light->GetLightData().Direction * length, 1.0f, scale.x, scale.x / 2, Vector4(0.8f, 0.2f, 0.3f, 1.0f));
    object->ArrowSegementObject->ConeObject->RenderObjects[0]->IsHiddenBoundBox = true;
    object->ArrowSegementObject->SegmentObject->RenderObjects[0]->IsHiddenBoundBox = true;
    object->Pos = pos;
    object->Light = light;
    object->PostUpdateFunc = [length](jObject* thisObject, float deltaTime)
    {
        auto thisDirectionalLightObject = static_cast<jDirectionalLightPrimitive*>(thisObject);
        thisDirectionalLightObject->BillboardObject->RenderObjects[0]->SetPos(thisDirectionalLightObject->Pos);
        thisDirectionalLightObject->ArrowSegementObject->SetPos(thisDirectionalLightObject->Pos);
        thisDirectionalLightObject->ArrowSegementObject->SetEnd(thisDirectionalLightObject->Light->GetLightData().Direction * length);
    };
	object->SkipShadowMapGen = true;
	object->SkipUpdateShadowVolume = true;
	light->LightDebugObject = object;
	return object;
}

jPointLightPrimitive* CreatePointLightDebug(const Vector& scale, jCamera* targetCamera, jPointLight* light, const char* textureFilename)
{
	jPointLightPrimitive* object = new jPointLightPrimitive();

	std::weak_ptr<jImageData> data = jImageFileLoader::GetInstance().LoadImageDataFromFile(jName(textureFilename), true);
	const jPointLightUniformBufferData& LightData = light->GetLightData();
	object->BillboardObject = jPrimitiveUtil::CreateBillobardQuad(LightData.Position, Vector::OneVector, scale, Vector4(1.0f), targetCamera);
	if (data.lock()->ImageData.size() > 0)
	{
		auto texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName(textureFilename), true).lock().get();
        object->BillboardObject->RenderObjects[0]->MaterialPtr = std::make_shared<jMaterial>();
        object->BillboardObject->RenderObjects[0]->MaterialPtr->TexData[static_cast<int32>(jMaterial::EMaterialTextureType::Albedo)].Texture = texture;
		object->BillboardObject->RenderObjects[0]->IsHiddenBoundBox = true;
	}
	object->SphereObject = CreateSphere(LightData.Position, LightData.MaxDistance, 20, Vector::OneVector, Vector4(LightData.Color, 1.0f), true, false);
	object->SphereObject->RenderObjects[0]->IsHiddenBoundBox = true;
	object->Light = light;
	object->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		auto pointLightPrimitive = static_cast<jPointLightPrimitive*>(thisObject);
		const jPointLightUniformBufferData& LightData = pointLightPrimitive->Light->GetLightData();
		pointLightPrimitive->BillboardObject->RenderObjects[0]->SetPos(LightData.Position);
		pointLightPrimitive->SphereObject->RenderObjects[0]->SetPos(LightData.Position);
		pointLightPrimitive->SphereObject->RenderObjects[0]->SetScale(Vector(LightData.MaxDistance));
	};
	object->SkipShadowMapGen = true;
	object->SkipUpdateShadowVolume = true;
	light->LightDebugObject = object;
	return object;
}

jSpotLightPrimitive* CreateSpotLightDebug(const Vector& scale, jCamera* targetCamera, jSpotLight* light, const char* textureFilename)
{
	jSpotLightPrimitive* object = new jSpotLightPrimitive();

	std::weak_ptr<jImageData> data = jImageFileLoader::GetInstance().LoadImageDataFromFile(jName(textureFilename), true);
	const jSpotLightUniformBufferData& LightData = light->GetLightData();
	object->BillboardObject = jPrimitiveUtil::CreateBillobardQuad(LightData.Position, Vector::OneVector, scale, Vector4(1.0f), targetCamera);
	if (data.lock()->ImageData.size() > 0)
	{
		auto texture = jImageFileLoader::GetInstance().LoadTextureFromFile(jName(textureFilename), true).lock().get();
        object->BillboardObject->RenderObjects[0]->MaterialPtr = std::make_shared<jMaterial>();
        object->BillboardObject->RenderObjects[0]->MaterialPtr->TexData[static_cast<int32>(jMaterial::EMaterialTextureType::Albedo)].Texture = texture;
	}
	object->UmbraConeObject = jPrimitiveUtil::CreateCone(LightData.Position, 1.0, 1.0, 20, Vector::OneVector, Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, 1.0f), true, false);
	object->UmbraConeObject->RenderObjects[0]->IsHiddenBoundBox = true;
	object->PenumbraConeObject = jPrimitiveUtil::CreateCone(LightData.Position, 1.0, 1.0, 20, Vector::OneVector, Vector4(LightData.Color.x, LightData.Color.y, LightData.Color.z, 0.5f), true, false);
	object->PenumbraConeObject->RenderObjects[0]->IsHiddenBoundBox = true;
	object->Light = light;
	object->PostUpdateFunc = [](jObject* thisObject, float deltaTime)
	{
		auto spotLightObject = static_cast<jSpotLightPrimitive*>(thisObject);
		const jSpotLightUniformBufferData& LightData = spotLightObject->Light->GetLightData();
		spotLightObject->BillboardObject->RenderObjects[0]->SetPos(LightData.Position);

		const auto lightDir = -LightData.Direction;
		const auto directionToRot = lightDir.GetEulerAngleFrom();
		const auto spotLightPos = LightData.Position + lightDir * (-spotLightObject->UmbraConeObject->RenderObjects[0]->GetScale().y / 2.0f);

		const auto umbraRadius = tanf(LightData.UmbraRadian) * LightData.MaxDistance;
		spotLightObject->UmbraConeObject->RenderObjects[0]->SetScale(Vector(umbraRadius, LightData.MaxDistance, umbraRadius));
		spotLightObject->UmbraConeObject->RenderObjects[0]->SetPos(spotLightPos);
		spotLightObject->UmbraConeObject->RenderObjects[0]->SetRot(directionToRot);

		const auto penumbraRadius = tanf(LightData.PenumbraRadian) * LightData.MaxDistance;
		spotLightObject->PenumbraConeObject->RenderObjects[0]->SetScale(Vector(penumbraRadius, LightData.MaxDistance, penumbraRadius));
		spotLightObject->PenumbraConeObject->RenderObjects[0]->SetPos(spotLightPos);
		spotLightObject->PenumbraConeObject->RenderObjects[0]->SetRot(directionToRot);
	};
	object->SkipShadowMapGen = true;
	object->SkipUpdateShadowVolume = true;
	light->LightDebugObject = object;
	return object;
}

jFrustumPrimitive* CreateFrustumDebug(const jCamera* targetCamera)
{
	jFrustumPrimitive* frustumPrimitive = new jFrustumPrimitive(targetCamera);
	for (int32 i = 0; i < 16; ++i)
	{
		frustumPrimitive->Segments[i] = CreateSegment(Vector::ZeroVector, Vector::ZeroVector, 1.0f, Vector4(1.0f));
		frustumPrimitive->Segments[i]->IsPostUpdate = false;
	}

	for (int32 i = 0; i < 6; ++i)
		frustumPrimitive->Plane[i] = CreateQuad(Vector::ZeroVector, Vector::OneVector, Vector::OneVector, Vector4(1.0f));

	return frustumPrimitive;
}

jGraph2D* CreateGraph2D(const Vector2& pos, const Vector2& size, const std::vector<Vector2>& points)
{
	const Vector2 point[4] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f} };

	const int32 elementCount = 4;

	// attribute 추가
	auto vertexStreamData = std::make_shared<jVertexStreamData>();

	{
		auto streamParam = std::make_shared<jStreamParam<Vector2>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(Vector2)));
		streamParam->Stride = sizeof(Vector2);
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(_countof(point));
		memcpy(&streamParam->Data[0], &point[0], sizeof(point));
		vertexStreamData->Params.push_back(streamParam);
	}

	{
		auto streamParam = std::make_shared<jStreamParam<Matrix>>();
		streamParam->BufferType = EBufferType::DYNAMIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(Matrix)));
		streamParam->Stride = sizeof(Matrix);
		streamParam->Name = jName("Transform");
		streamParam->Data.clear();
		streamParam->InstanceDivisor = 1;
		vertexStreamData->Params.push_back(streamParam);
	}

	vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLE_STRIP;
	vertexStreamData->ElementCount = elementCount;

	auto object = new jGraph2D();
	auto renderObject = new jRenderObject();
    object->RenderObjectGeometryDataPtr = std::make_shared<jRenderObjectGeometryData>(vertexStreamData, nullptr);
    renderObject->CreateRenderObject(object->RenderObjectGeometryDataPtr);
    object->RenderObjects.push_back(renderObject);
	object->SethPos(pos);
	object->SetGuardLineSize(size);
	object->SetPoints(points);

	return object;
}

}

void jDirectionalLightPrimitive::Update(float deltaTime)
{
	__super::Update(deltaTime);

	if (BillboardObject)
		BillboardObject->Update(deltaTime);
	if (ArrowSegementObject)
		ArrowSegementObject->Update(deltaTime);
}


//void jDirectionalLightPrimitive::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	__super::Draw(InRenderFrameContext, camera, shader, lights);
//
//	if (BillboardObject)
//		BillboardObject->Draw(InRenderFrameContext, camera, shader, lights);
//	if (ArrowSegementObject)
//		ArrowSegementObject->Draw(InRenderFrameContext, camera, shader, lights);
//}

void jSegmentPrimitive::UpdateSegment()
{
	if (RenderObjectGeometryDataPtr->VertexStream->Params.size() < 2)
	{
		JASSERT(0);
		return;
	}

	RenderObjectGeometryDataPtr->VertexStream->Params[0].reset();
	RenderObjectGeometryDataPtr->VertexStream->Params[1].reset();
	const auto currentEnd = GetCurrentEnd();

	const float vertices[] = {
	Start.x, Start.y, Start.z,
	currentEnd.x, currentEnd.y, currentEnd.z,
	};

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::DYNAMIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
		streamParam->Stride = sizeof(float) * 3;
		streamParam->Name = jName("POSITION");
		streamParam->Data.resize(_countof(vertices));
		memcpy(&streamParam->Data[0], &vertices[0], sizeof(vertices));
		RenderObjectGeometryDataPtr->VertexStream->Params[0] = streamParam;
	}

	{
		auto streamParam = std::make_shared<jStreamParam<float>>();
		streamParam->BufferType = EBufferType::STATIC;
		streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
		streamParam->Stride = sizeof(float) * 4;
		streamParam->Name = jName("COLOR");
		streamParam->Data = std::move(jPrimitiveUtil::GenerateColor(Color, 2));
		RenderObjectGeometryDataPtr->VertexStream->Params[1] = streamParam;
	}
	g_rhi->UpdateVertexBuffer(RenderObjectGeometryDataPtr->VertexBuffer, RenderObjectGeometryDataPtr->VertexStream);
}

void jSegmentPrimitive::UpdateSegment(const Vector& start, const Vector& end, const Vector4& color, float time)
{
	Start = start;
	End = end;
	Color = color;
	Time = time;
	UpdateSegment();
}

void jSegmentPrimitive::Update(float deltaTime)
{
	__super::Update(deltaTime);

	UpdateSegment();
}

void jPointLightPrimitive::Update(float deltaTime)
{
	__super::Update(deltaTime);
	BillboardObject->Update(deltaTime);
	SphereObject->Update(deltaTime);
}

//void jPointLightPrimitive::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	__super::Draw(InRenderFrameContext, camera, shader, lights);
//	BillboardObject->Draw(InRenderFrameContext, camera, shader, lights);
//	SphereObject->Draw(InRenderFrameContext, camera, shader, lights);
//}

void jSpotLightPrimitive::Update(float deltaTime)
{
	__super::Update(deltaTime);
	BillboardObject->Update(deltaTime);
	UmbraConeObject->Update(deltaTime);
	PenumbraConeObject->Update(deltaTime);
}

//void jSpotLightPrimitive::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	__super::Draw(InRenderFrameContext, camera, shader, lights);
//	BillboardObject->Draw(InRenderFrameContext, camera, shader, lights);
//	UmbraConeObject->Draw(InRenderFrameContext, camera, shader, lights);
//	PenumbraConeObject->Draw(InRenderFrameContext, camera, shader, lights);
//}

void jFrustumPrimitive::Update(float deltaTime)
{
	Vector far_lt;
	Vector far_rt;
	Vector far_lb;
	Vector far_rb;

	Vector near_lt;
	Vector near_rt;
	Vector near_lb;
	Vector near_rb;

	const auto origin = TargetCamera->Pos;
	const float n = TargetCamera->Near;
	const float f = TargetCamera->Far;

	if (TargetCamera->IsPerspectiveProjection)
	{
		const float InvAspect = ((float)TargetCamera->Width / (float)TargetCamera->Height);
		const float length = tanf(TargetCamera->FOVRad * 0.5f);
		Vector targetVec = TargetCamera->GetForwardVector().GetNormalize();
		Vector rightVec = TargetCamera->GetRightVector() * length * InvAspect;
		Vector upVec = TargetCamera->GetUpVector() * length;

		Vector rightUp = (targetVec + rightVec + upVec);
		Vector leftUp = (targetVec - rightVec + upVec);
		Vector rightDown = (targetVec + rightVec - upVec);
		Vector leftDown = (targetVec - rightVec - upVec);

		far_lt = origin + leftUp * f;
		far_rt = origin + rightUp * f;
		far_lb = origin + leftDown * f;
		far_rb = origin + rightDown * f;

		near_lt = origin + leftUp * n;
		near_rt = origin + rightUp * n;
		near_lb = origin + leftDown * n;
		near_rb = origin + rightDown * n;

		if (PostPerspective)
		{
			auto ProjView = TargetCamera->Projection * TargetCamera->View;

			far_lt = ProjView.TransformPoint(far_lt);
			far_rt = ProjView.TransformPoint(far_rt);
			far_lb = ProjView.TransformPoint(far_lb);
			far_rb = ProjView.TransformPoint(far_rb);

			near_lt = ProjView.TransformPoint(near_lt);
			near_rt = ProjView.TransformPoint(near_rt);
			near_lb = ProjView.TransformPoint(near_lb);
			near_rb = ProjView.TransformPoint(near_rb);
		}
	}
	else
	{
		const float w = (float)TargetCamera->Width;
		const float h = (float)TargetCamera->Height;

		Vector targetVec = TargetCamera->GetForwardVector().GetNormalize();
		Vector rightVec = TargetCamera->GetRightVector().GetNormalize();
		Vector upVec = TargetCamera->GetUpVector().GetNormalize();

		far_lt = origin + targetVec * f - rightVec * w * 0.5f + upVec * h * 0.5f;
		far_rt = origin + targetVec * f + rightVec * w * 0.5f + upVec * h * 0.5f;
		far_lb = origin + targetVec * f - rightVec * w * 0.5f - upVec * h * 0.5f;
		far_rb = origin + targetVec * f + rightVec * w * 0.5f - upVec * h * 0.5f;

		near_lt = origin + targetVec * n - rightVec * w * 0.5f + upVec * h * 0.5f;
		near_rt = origin + targetVec * n + rightVec * w * 0.5f + upVec * h * 0.5f;
		near_lb = origin + targetVec * n - rightVec * w * 0.5f - upVec * h * 0.5f;
		near_rb = origin + targetVec * n + rightVec * w * 0.5f - upVec * h * 0.5f;
	}

	const Vector4 baseColor = (TargetCamera->IsPerspectiveProjection ? Vector4(1.0f) : Vector4(0.0f, 0.0f, 1.0f, 1.0f));
	Segments[0]->UpdateSegment(near_rt, far_rt, baseColor);
	Segments[1]->UpdateSegment(near_lt, far_lt, baseColor);
	Segments[2]->UpdateSegment(near_rb, far_rb, baseColor);
	Segments[3]->UpdateSegment(near_lb, far_lb, baseColor);

	const Vector4 green(0.0f, 1.0f, 0.0f, 1.0f);
	Segments[4]->UpdateSegment(near_lt, near_rt, green);
	Segments[5]->UpdateSegment(near_lb, near_rb, green);
	Segments[6]->UpdateSegment(near_lt, near_lb, Vector4(1.0f, 1.0f, 0.0f, 1.0f));
	Segments[7]->UpdateSegment(near_rt, near_rb, green);

	const Vector4 red(1.0f, 0.0f, 0.0f, 1.0f);
	Segments[8]->UpdateSegment(far_lt, far_rt, red);
	Segments[9]->UpdateSegment(far_lb, far_rb, red);
	Segments[10]->UpdateSegment(far_lt, far_lb, Vector4(0.0f, 1.0f, 1.0f, 1.0f));
	Segments[11]->UpdateSegment(far_rt, far_rb, red);

	Segments[12]->UpdateSegment(far_rt, near_rt, baseColor);
	Segments[13]->UpdateSegment(far_rb, near_rb, baseColor);
	Segments[14]->UpdateSegment(far_lb, near_lb, baseColor);
	Segments[15]->UpdateSegment(far_rb, near_rb, baseColor);

	auto updateQuadFunc = [this](jQuadPrimitive* quad, const Vector& p1, const Vector& p2, const Vector& p3, const Vector& p4, const Vector4& color)
	{
		float vertices[] = {
			p1.x, p1.y, p1.z,
			p2.x, p2.y, p2.z,
			p3.x, p3.y, p3.z,
			p3.x, p3.y, p3.z,
			p2.x, p2.y, p2.z,
			p4.x, p4.y, p4.z,
		};

		float colors[] = {
			color.x, color.y, color.z, color.w,
			color.x, color.y, color.z, color.w,
			color.x, color.y, color.z, color.w,
			color.x, color.y, color.z, color.w,
			color.x, color.y, color.z, color.w,
			color.x, color.y, color.z, color.w,
			color.x, color.y, color.z, color.w,
		};

		const int32 elementCount = static_cast<int32>(_countof(vertices) / 3);

		// attribute 추가
		auto vertexStreamData = std::make_shared<jVertexStreamData>();

		{
			auto streamParam = std::make_shared<jStreamParam<float>>();
			streamParam->BufferType = EBufferType::STATIC;
			streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
			streamParam->Stride = sizeof(float) * 3;
			streamParam->Name = jName("POSITION");
			streamParam->Data.resize(elementCount * 3);
			memcpy(&streamParam->Data[0], &vertices[0], sizeof(vertices));
			vertexStreamData->Params.push_back(streamParam);
		}

		{
			auto streamParam = std::make_shared<jStreamParam<float>>();
			streamParam->BufferType = EBufferType::STATIC;
			streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 4));
			streamParam->Stride = sizeof(float) * 4;
			streamParam->Name = jName("COLOR");
			streamParam->Data = std::move(jPrimitiveUtil::GenerateColor(color, elementCount));
			vertexStreamData->Params.push_back(streamParam);
		}

		std::vector<float> normals(elementCount * 3);
		for (int32 i = 0; i < elementCount; ++i)
		{
			normals[i * 3] = 0.0f;
			normals[i * 3 + 1] = 1.0f;
			normals[i * 3 + 2] = 0.0f;
		}

		{
			auto streamParam = std::make_shared<jStreamParam<float>>();
			streamParam->BufferType = EBufferType::STATIC;
			streamParam->Attributes.push_back(IStreamParam::jAttribute(EBufferElementType::FLOAT, sizeof(float) * 3));
			streamParam->Stride = sizeof(float) * 3;
			streamParam->Name = jName("NORMAL");
			streamParam->Data.resize(elementCount * 3);
			memcpy(&streamParam->Data[0], &normals[0], normals.size() * sizeof(float));
			vertexStreamData->Params.push_back(streamParam);
		}

		vertexStreamData->PrimitiveType = EPrimitiveType::TRIANGLES;
		vertexStreamData->ElementCount = elementCount;

		quad->RenderObjectGeometryDataPtr->UpdateVertexStream(vertexStreamData);
	};

	updateQuadFunc(Plane[0], far_lt, near_lt, far_lb, near_lb, Vector4(0.0f, 1.0f, 0.0f, 0.3f));
	updateQuadFunc(Plane[1], near_rt, far_rt, near_rb, far_rb, Vector4(0.0f, 0.0f, 1.0f, 0.3f));
	updateQuadFunc(Plane[2], far_lt, far_rt, near_lt, near_rt, Vector4(1.0f, 1.0f, 0.0f, 0.3f));
	updateQuadFunc(Plane[3], near_lb, near_rb, far_lb, far_rb, Vector4(1.0f, 0.0f, 0.0f, 0.3f));
	updateQuadFunc(Plane[4], near_lt, near_rt, near_lb, near_rb, Vector4(1.0f, 1.0f, 1.0f, 0.3f));
	updateQuadFunc(Plane[5], far_lt, far_rt, far_lb, far_rb, Vector4(1.0f, 1.0f, 1.0f, 0.3f));

	for (int32 i = 0; i < 16; ++i)
	{
		Segments[i]->RenderObjects[0]->SetPos(Offset);
		Segments[i]->RenderObjects[0]->SetScale(Scale);
		Segments[i]->Update(deltaTime);
	}

	for (int32 i = 0; i < 6; ++i)
	{
		Plane[i]->RenderObjects[0]->SetPos(Offset);
		Plane[i]->RenderObjects[0]->SetScale(Scale);
		Plane[i]->Update(deltaTime);
	}
}

//void jFrustumPrimitive::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	__super::Draw(InRenderFrameContext, camera, shader, lights);
//	for (int32 i = 0; i < 16; ++i)
//		Segments[i]->Draw(InRenderFrameContext, camera, shader, lights);
//
//	if (DrawPlane)
//	{
//		for (int32 i = 0; i < 6; ++i)
//			Plane[i]->Draw(InRenderFrameContext, camera, shader, lights);
//	}
//}

void jGraph2D::Update(float deltaTime)
{
	UpdateBuffer();
}

//void jGraph2D::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	SET_UNIFORM_BUFFER_STATIC("InvViewportSize", Vector2(1.0f / SCR_WIDTH, 1.0f / SCR_HEIGHT), shader);
//	SET_UNIFORM_BUFFER_STATIC("LineColor", Vector4::ColorRed, shader);
//	SET_UNIFORM_BUFFER_STATIC("GuardLineColor", Vector4::ColorWhite, shader);
//	__super::Draw(InRenderFrameContext, camera, shader, lights, static_cast<int32>(ResultMatrices.size()));
//}

void jGraph2D::SethPos(const Vector2& pos)
{
	if (Pos != pos)
	{
		DirtyFlag = true;
		Pos = pos;
	}
}

void jGraph2D::SetPoints(const std::vector<Vector2>& points)
{
	Points = points;
	DirtyFlag = true;
}

void jGraph2D::UpdateBuffer()
{
	if (DirtyFlag)
	{
		DirtyFlag = false;

		ResultPoints.resize(Points.size());
		for (int32 i = 0; i < ResultPoints.size(); ++i)
			ResultPoints[i] = Vector2(Points[i].x, -Points[i].y) + Vector2(Pos.x, Pos.y);

		if (ResultPoints.size() > 1)
		{
			ResultMatrices.resize(ResultPoints.size() - 1 + 2);

			{
				Matrix hor;
				hor.SetIdentity();
				hor.SetXBasis(Vector4::RightVector);
				hor.SetYBasis(Vector4::UpVector);
				hor.SetZBasis(Vector4::FowardVector);
				hor.SetTranslate(Pos.x, Pos.y, 0.0f);
				hor = (hor * Matrix::MakeScale(GuardLineSize.x, 1.0f, 1.0f));
				ResultMatrices[0] = (hor);

				Matrix ver;
				ver.SetIdentity();
				ver.SetXBasis(Vector4::RightVector);
				ver.SetYBasis(Vector4::UpVector);
				ver.SetZBasis(Vector4::FowardVector);
				ver.SetTranslate(Pos.x, Pos.y, 0.0f);
				ver = (ver * Matrix::MakeScale(1.0f, -GuardLineSize.y, 1.0f));
				ResultMatrices[1] = (ver);
			}

			for (int i = 2; i < ResultMatrices.size(); ++i)
			{
				const auto& p2 = ResultPoints[i + 1];
				const auto& p1 = ResultPoints[i];
				auto right = Vector(p2 - p1, 0.0f);
				const float lineLength = right.Length();
				right.SetNormalize();
				right.z = 0.0f;

				auto up = right.CrossProduct(Vector::FowardVector);
				up.SetNormalize();

				Matrix& tr = ResultMatrices[i];
				tr.SetIdentity();
				tr.SetXBasis(Vector4(right, 0.0f));
				tr.SetYBasis(Vector4(up, 0.0f));
				tr.SetZBasis(Vector4::FowardVector);
				tr.SetTranslate(p1.x, p1.y, 0.0f);
				tr = (tr * Matrix::MakeScale(lineLength, 1.0f, 1.0f));
			}

			if (RenderObjectGeometryDataPtr->VertexStream)
			{
				static jName TransformName("Transform");
				for (auto& iter : RenderObjectGeometryDataPtr->VertexStream->Params)
				{
					if (iter->Name == TransformName)
					{
						auto matStreamParam = static_cast<jStreamParam<Matrix>*>(iter.get());
						matStreamParam->Data.resize(ResultMatrices.size());
						memcpy(&matStreamParam->Data[0], &ResultMatrices[0], ResultMatrices.size() * sizeof(Matrix));
						break;
					}
				}
				RenderObjectGeometryDataPtr->UpdateVertexStream();
			}
		}
		else
		{
			ResultMatrices.clear();
		}
	}
}
