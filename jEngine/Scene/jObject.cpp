#include "pch.h"
#include "jObject.h"
#include "jRenderObject.h"
#include "jPrimitiveUtil.h"

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#endif

jObjectID jObject::s_NextObjectID = 1;  // Start from 1, 0 means no object
std::unordered_map<jObjectID, jObject*> jObject::s_ObjectIDMap;

std::vector<jObject*> jObject::s_ShadowCasterObject;
std::vector<jRenderObject*> jObject::s_ShadowCasterRenderObject;
std::vector<jObject*> jObject::s_StaticObjects;
std::vector<jRenderObject*> jObject::s_StaticRenderObjects;
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

	{
		for (auto& RenderObject : object->RenderObjects)
		{
			// Ensure Owner is set for ObjectID picking
			if (!RenderObject->Owner)
				RenderObject->Owner = object;

			s_StaticRenderObjects.push_back(RenderObject);
            if (!object->SkipShadowMapGen)
                s_ShadowCasterRenderObject.push_back(RenderObject);
		}
	}

#ifdef ENABLE_EDITOR_FEATURES
	// Register with PlacementTool so all static objects appear in the placement list
	if (g_Editor)
		g_Editor->Placement.RegisterStaticObject(object);
#endif
}

void jObject::RemoveObject(jObject* object)
{
	if (!object)
		return;
	std::erase_if(s_ShadowCasterObject, [&object](jObject* param)
	{
		return (param == object);
	});
	std::erase_if(s_StaticObjects, [&object](jObject* param)
	{
		return (param == object);
	});

	{
		for (auto& RenderObject : object->RenderObjects)
		{
			std::erase_if(s_ShadowCasterRenderObject, [&RenderObject](jRenderObject* param)
			{
				return (param == RenderObject);
			});

			std::erase_if(s_StaticRenderObjects, [&RenderObject](jRenderObject* param)
			{
				return (param == RenderObject);
			});
		}
	}

#ifdef ENABLE_EDITOR_FEATURES
	// Unregister from PlacementTool
	if (g_Editor)
		g_Editor->Placement.UnregisterStaticObject(object);
#endif
}

void jObject::FlushDirtyState()
{
	if (!s_DirtyStateObjects.empty())
	{
		for (auto iter : s_DirtyStateObjects)
		{
			auto it_find = std::find(s_ShadowCasterObject.begin(), s_ShadowCasterObject.end(), iter);
			const bool existInShadowCasterObject = s_ShadowCasterObject.end() != it_find;
			//if (iter->SkipShadowMapGen)
			//{
			//	if (existInShadowCasterObject)
			//		s_ShadowCasterObject.erase(it_find);
			//}
			//else
			//{
			//	if (!existInShadowCasterObject)
			//		s_ShadowCasterObject.push_back(iter);
			//}
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
	std::erase_if(s_BoundBoxObjects, [&object](jObject* param)
	{
		return (param == object);
	});
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
	std::erase_if(s_BoundSphereObjects, [&object](jObject* param)
	{
		return (param == object);
	});
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
	std::erase_if(s_DebugObjects, [&object](jObject* param)
	{
		return (param == object);
	});
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
	std::erase_if(s_UIObjects, [&object](jObject* param)
	{
		return (param == object);
	});
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
	std::erase_if(s_UIDebugObjects, [&object](jObject* param)
	{
		return (param == object);
	});
}
//////////////////////////////////////////////////////////////////////////
jObject::jObject()
{
	// Assign unique ObjectID
	ObjectID = s_NextObjectID++;
	s_ObjectIDMap[ObjectID] = this;
}


jObject::~jObject()
{
	// Remove from ObjectID map
	s_ObjectIDMap.erase(ObjectID);

	jObject::RemoveBoundBoxObject(BoundBoxObject);
	delete BoundBoxObject;

	jObject::RemoveBoundSphereObject(BoundSphereObject);
	delete BoundSphereObject;

	for(auto& RenderObject : RenderObjects)
		delete RenderObject;
	RenderObjects.clear();
}

void jObject::AddRenderObject(jRenderObject* renderObject)
{
	if (!renderObject)
		return;

	// Set owner for ObjectID picking
	renderObject->Owner = this;

	RenderObjects.push_back(renderObject);
}

jObject* jObject::FindObjectByID(jObjectID id)
{
	auto it = s_ObjectIDMap.find(id);
	if (it != s_ObjectIDMap.end())
		return it->second;
	return nullptr;
}

void jObject::Update(float deltaTime)
{
	if (IsPostUpdate && PostUpdateFunc)
		PostUpdateFunc(this, deltaTime);
}

//void jObject::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera
//	, const jShader* shader, const std::list<const jLight*>& lights, int32 instanceCount /*= 0*/) const
//{
//	if (Visible && RenderObject)
//		RenderObject->Draw(InRenderFrameContext, 0, -1, 0, -1, instanceCount);
//}

//void jObject::CreateBoundBox(bool isShow)
//{
//	if (RenderObject)
//	{
//		BoundBox.CreateBoundBox(RenderObject->GetVertices());
//		BoundBoxObject = jPrimitiveUtil::CreateBoundBox(BoundBox, this);
//		
//		BoundSphere.CreateBoundSphere(RenderObject->GetVertices());
//		BoundSphereObject = jPrimitiveUtil::CreateBoundSphere(BoundSphere, this);
//
//		if (isShow)
//		{
//			jObject::AddBoundBoxObject(BoundBoxObject);
//			jObject::AddBoundSphereObject(BoundSphereObject);
//		}
//	}
//}

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

bool jObject::HasInstancing() const
{
	return RenderObjects[0] ? RenderObjects[0]->HasInstancing() : false;
}
