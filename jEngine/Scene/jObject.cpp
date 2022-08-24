#include "pch.h"
#include "jObject.h"
#include "jRenderObject.h"
#include "jPrimitiveUtil.h"

std::vector<jObject*> jObject::s_ShadowCasterObject;
std::vector<jObject*> jObject::s_StaticObjects;
std::vector<jObject*> jObject::s_BoundBoxObjects;
std::vector<jObject*> jObject::s_BoundSphereObjects;
std::vector<jObject*> jObject::s_DebugObjects;
std::set<jObject*> jObject::s_DirtyStateObjects;
std::vector<jObject*> jObject::s_UIObjects;
std::vector<jObject*> jObject::s_UIDebugObjects;

//std::list<jObject*> g_StaticObjectArray;
std::vector<jObject*> g_HairObjectArray;

void jObject::AddObject(jObject* object)
{
	if (!object)
		return;

	//g_StaticObjectArray.push_back(object);

	if (!object->SkipShadowMapGen)
	{
		//if (object->RenderObject && object->RenderObject->VertexStream)
		//	JASSERT(object->RenderObject->VertexStream->PrimitiveType == EPrimitiveType::TRIANGLES);

		s_ShadowCasterObject.push_back(object);
	}
	s_StaticObjects.push_back(object);
}

void jObject::RemoveObject(jObject* object)
{
	if (!object)
		return;
	s_ShadowCasterObject.erase(std::remove_if(s_ShadowCasterObject.begin(), s_ShadowCasterObject.end(), [&object](jObject* param)
	{
		return (param == object);
	}));
	s_StaticObjects.erase(std::remove_if(s_StaticObjects.begin(), s_StaticObjects.end(), [&object](jObject* param)
	{
		return (param == object);
	}));
}

void jObject::FlushDirtyState()
{
	if (!s_DirtyStateObjects.empty())
	{
		for (auto iter : s_DirtyStateObjects)
		{
			auto it_find = std::find(s_ShadowCasterObject.begin(), s_ShadowCasterObject.end(), iter);
			const bool existInShadowCasterObject = s_ShadowCasterObject.end() != it_find;
			if (iter->SkipShadowMapGen)
			{
				if (existInShadowCasterObject)
					s_ShadowCasterObject.erase(it_find);
			}
			else
			{
				if (!existInShadowCasterObject)
					s_ShadowCasterObject.push_back(iter);
			}
		}
	}
}

void jObject::AddBoundBoxObject(jObject* object)
{
	if (!object)
		return;
	s_BoundBoxObjects.push_back(object);
}

void jObject::RemoveBoundBoxObject(jObject* object)
{
	if (!object)
		return;
	s_BoundBoxObjects.erase(std::remove_if(s_BoundBoxObjects.begin(), s_BoundBoxObjects.end(), [&object](jObject* param)
	{
		return (param == object);
	}));
}

void jObject::AddBoundSphereObject(jObject* object)
{
	if (!object)
		return;
	s_BoundSphereObjects.push_back(object);
}

void jObject::RemoveBoundSphereObject(jObject* object)
{
	if (!object)
		return;
	s_BoundSphereObjects.erase(std::remove_if(s_BoundSphereObjects.begin(), s_BoundSphereObjects.end(), [&object](jObject* param)
	{
		return (param == object);
	}));
}

void jObject::AddDebugObject(jObject* object)
{
	if (!object)
		return;
	s_DebugObjects.push_back(object);
}

void jObject::RemoveDebugObject(jObject* object)
{
	if (!object)
		return;
	s_DebugObjects.erase(std::remove_if(s_DebugObjects.begin(), s_DebugObjects.end(), [&object](jObject* param)
	{
		return (param == object);
	}));
}

void jObject::AddUIObject(jObject* object)
{
	if (!object)
		return;
	s_UIObjects.push_back(object);
}

void jObject::RemoveUIObject(jObject* object)
{
	if (!object)
		return;
	s_UIObjects.erase(std::remove_if(s_UIObjects.begin(), s_UIObjects.end(), [&object](jObject* param)
	{
		return (param == object);
	}));
}

void jObject::AddUIDebugObject(jObject* object)
{
	if (!object)
		return;
	s_UIDebugObjects.push_back(object);
}

void jObject::RemoveUIDebugObject(jObject* object)
{
	if (!object)
		return;
	s_UIDebugObjects.erase(std::remove_if(s_UIDebugObjects.begin(), s_UIDebugObjects.end(), [&object](jObject* param)
	{
		return (param == object);
	}));
}
//////////////////////////////////////////////////////////////////////////
jObject::jObject()
{
}


jObject::~jObject()
{
	jObject::RemoveBoundBoxObject(BoundBoxObject);
	delete BoundBoxObject;

	jObject::RemoveBoundSphereObject(BoundSphereObject);
	delete BoundSphereObject;

	delete RenderObject;
}

void jObject::Update(float deltaTime)
{
	if (IsPostUpdate && PostUpdateFunc)
		PostUpdateFunc(this, deltaTime);
}

void jObject::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera
	, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
{
	if (Visible && RenderObject)
		RenderObject->Draw(InRenderFrameContext, camera, shader, lights, 0, -1, instanceCount);
}

void jObject::CreateBoundBox(bool isShow)
{
	if (RenderObject)
	{
		BoundBox.CreateBoundBox(RenderObject->GetVertices());
		BoundBoxObject = jPrimitiveUtil::CreateBoundBox(BoundBox, this);
		
		BoundSphere.CreateBoundSphere(RenderObject->GetVertices());
		BoundSphereObject = jPrimitiveUtil::CreateBoundSphere(BoundSphere, this);

		if (isShow)
		{
			jObject::AddBoundBoxObject(BoundBoxObject);
			jObject::AddBoundSphereObject(BoundSphereObject);
		}
	}
}

void jObject::ShowBoundBox(bool isShow)
{
	if (isShow)
	{
		jObject::AddBoundBoxObject(BoundBoxObject);
		jObject::AddBoundSphereObject(BoundSphereObject);
	}
	else
	{
		jObject::RemoveBoundBoxObject(BoundBoxObject);
		jObject::RemoveBoundSphereObject(BoundSphereObject);
	}
}
