#include "pch.h"
#include "jObject.h"
#include "jRenderObject.h"
#include "jVertexAdjacency.h"

std::list<jObject*> jObject::s_ShadowCasterObject;
std::list<jObject*> jObject::s_StaticObjects;
std::list<jObject*> jObject::s_BoundBoxObjects;
std::list<jObject*> jObject::s_BoundSphereObjects;
std::list<jObject*> jObject::s_DebugObjects;
std::set<jObject*> jObject::s_DirtyStateObjects;
std::list<jObject*> jObject::s_UIObjects;
std::list<jObject*> jObject::s_UIDebugObjects;

//std::list<jObject*> g_StaticObjectArray;
std::list<jObject*> g_HairObjectArray;

void jObject::AddObject(jObject* object)
{
	//g_StaticObjectArray.push_back(object);

	if (!object->SkipShadowMapGen)
		s_ShadowCasterObject.push_back(object);
	s_StaticObjects.push_back(object);
}

void jObject::RemoveObject(jObject* object)
{
	s_ShadowCasterObject.remove_if([&object](jObject* param)
	{
		return (param == object);
	});
	s_StaticObjects.remove_if([&object](jObject* param)
	{
		return (param == object);
	});
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
	s_BoundBoxObjects.push_back(object);
}

void jObject::RemoveBoundBoxObject(jObject* object)
{
	s_BoundBoxObjects.remove_if([&object](jObject* param)
	{
		return (param == object);
	});
}

void jObject::AddBoundSphereObject(jObject* object)
{
	s_BoundSphereObjects.push_back(object);
}

void jObject::RemoveBoundSphereObject(jObject* object)
{
	s_BoundSphereObjects.remove_if([&object](jObject* param)
	{
		return (param == object);
	});
}

void jObject::AddDebugObject(jObject* object)
{
	s_DebugObjects.push_back(object);
}

void jObject::RemoveDebugObject(jObject* object)
{
	s_DebugObjects.remove_if([&object](jObject* param)
		{
			return (param == object);
		});
}

void jObject::AddUIObject(jObject* object)
{
	s_UIObjects.push_back(object);
}

void jObject::RemoveUIObject(jObject* object)
{
	s_UIObjects.remove_if([&object](jObject* param)
		{
			return (param == object);
		});
}

void jObject::AddUIDebugObject(jObject* object)
{
	s_UIDebugObjects.push_back(object);
}

void jObject::RemoveUIDebugObject(jObject* object)
{
	s_UIDebugObjects.remove_if([&object](jObject* param)
		{
			return (param == object);
		});
}
//////////////////////////////////////////////////////////////////////////
jObject::jObject()
{
}


jObject::~jObject()
{
}

void jObject::Update(float deltaTime)
{
	if (VertexAdjacency)
		VertexAdjacency->Update(this);

	if (PostUpdateFunc)
		PostUpdateFunc(this, deltaTime);
}

void jObject::Draw(const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights)
{
	if (Visible && RenderObject)
		RenderObject->Draw(camera, shader, lights);
}

//void jObject::Draw(const jCamera* camera, const jShader* shader)
//{
//	if (Visible && RenderObject)
//		RenderObject->Draw(camera, shader);
//}
