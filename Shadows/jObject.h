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
	static void FlushDirtyState();

	static const std::list<jObject*>& GetShadowCasterObject() { return s_ShadowCasterObject; }
	static const std::list<jObject*>& GetStaticObject() { return s_StaticObjects; }

	static void AddBoundBoxObject(jObject* object);
	static void RemoveBoundBoxObject(jObject* object);
	static void AddBoundSphereObject(jObject* object);
	static void RemoveBoundSphereObject(jObject* object);
	static void AddDebugObject(jObject* object);
	static void RemoveDebugObject(jObject* object);

	virtual void Update(float deltaTime);
	//virtual void Draw(const jCamera* camera, const jShader* shader);
	virtual void Draw(const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights);

	void SetDirtyState() { DirtyObjectState = true; s_DirtyStateObjects.insert(this); }

	void SetSkipShadowMapGen(bool skipShadowMapGen) { SkipShadowMapGen = skipShadowMapGen; SetDirtyState(); }
	void SetSkipUpdateShadowVolume(bool skipUpdateShadowVolume) { SkipUpdateShadowVolume = skipUpdateShadowVolume; SetDirtyState(); }
	void SetVisible(bool visible) { Visible = visible; SetDirtyState(); }

	bool SkipShadowMapGen = false;
	bool SkipUpdateShadowVolume = false;
	bool Visible = true;
	bool DirtyObjectState = false;
	
	std::function<void(jObject*, float)> PostUpdateFunc;

	// todo 현재는 보유만 하고있음.
	std::vector<jObject*> BoundObjects;

private:
	static std::list<jObject*> s_ShadowCasterObject;
	static std::list<jObject*> s_StaticObjects;
	static std::set<jObject*> s_DirtyStateObjects;
};

//extern std::list<jObject*> g_StaticObjectArray;
extern std::list<jObject*> g_HairObjectArray;
extern std::list<jObject*> g_DebugObjectArray;
extern std::list<jObject*> g_BoundBoxObjectArray;
extern std::list<jObject*> g_BoundSphereObjectArray;