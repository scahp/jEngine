#pragma once

class jRenderObject;
class jCamera;
struct jShader;
class jLight;
class jVertexAdjacency;
class jShadowVolume;

class jObject
{
public:
	jObject();
	~jObject();

	jRenderObject* RenderObject = nullptr;
	jVertexAdjacency* VertexAdjacency = nullptr;
	jShadowVolume* ShadowVolume = nullptr;

	static void AddObject(jObject* object);
	static void RemoveObject(jObject* object);

	static void AddBoundBoxObject(jObject* object);
	static void RemoveBoundBoxObject(jObject* object);
	static void AddBoundSphereObject(jObject* object);
	static void RemoveBoundSphereObject(jObject* object);
	static void AddDebugObject(jObject* object);
	static void RemoveDebugObject(jObject* object);

	virtual void Update(float deltaTime);
	virtual void Draw(jCamera* camera, jShader* shader);
	virtual void Draw(jCamera* camera, jShader* shader, jLight* light);

	bool SkipShadowMapGen = false;
	bool SkipUpdateShadowVolume = false;
	bool Visible = true;
	
	std::function<void(jObject*, float)> PostUpdateFunc;

	// todo 현재는 보유만 하고있음.
	std::vector<jObject*> BoundObjects;
};

extern std::list<jObject*> g_StaticObjectArray;
extern std::list<jObject*> g_HairObjectArray;
extern std::list<jObject*> g_DebugObjectArray;
extern std::list<jObject*> g_BoundBoxObjectArray;
extern std::list<jObject*> g_BoundSphereObjectArray;