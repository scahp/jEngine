#pragma once
#include "Math/Vector.h"
#include "Math/Matrix.h"

class jObject;

// Base scene object with transform capabilities
// This class provides position, rotation, scale and world matrix management
// Designed to be inherited by jRenderObject, jLight, and other scene entities
class jSceneObject
{
public:
	jSceneObject();
	virtual ~jSceneObject();

	// Owner object (for object picking and hierarchy)
	jObject* Owner = nullptr;

	// Transform accessors
	FORCEINLINE void SetPos(const Vector& InPos) { if (Pos.IsNearlyEqual(InPos)) return; Pos = InPos; SetDirtyFlags(EDirty::POS); }
	FORCEINLINE void SetRot(const Vector& InRot) { if (Rot.IsNearlyEqual(InRot)) return; Rot = InRot; SetDirtyFlags(EDirty::ROT); }
	FORCEINLINE void SetScale(const Vector& InScale) { if (Scale.IsNearlyEqual(InScale)) return; Scale = InScale; SetDirtyFlags(EDirty::SCALE); }
	FORCEINLINE const Vector& GetPos() const { return Pos; }
	FORCEINLINE const Vector& GetRot() const { return Rot; }
	FORCEINLINE const Vector& GetScale() const { return Scale; }

	// World matrix management
	virtual void UpdateWorldMatrix();
	Matrix World;

protected:
	// Dirty flags for transform optimization
	enum EDirty : int8
	{
		NONE = 0,
		POS = 1,
		ROT = 1 << 1,
		SCALE = 1 << 2,
		POS_ROT_SCALE = POS | ROT | SCALE,
	};

	EDirty DirtyFlags = EDirty::POS_ROT_SCALE;

	void SetDirtyFlags(EDirty InEnum)
	{
		using T = std::underlying_type<EDirty>::type;
		DirtyFlags = static_cast<EDirty>(static_cast<T>(InEnum) | static_cast<T>(DirtyFlags));
		OnTransformDirty();
	}

	void ClearDirtyFlags(EDirty InEnum)
	{
		using T = std::underlying_type<EDirty>::type;
		DirtyFlags = static_cast<EDirty>(static_cast<T>(InEnum) & (!static_cast<T>(DirtyFlags)));
	}

	FORCEINLINE void ClearDirtyFlags() { DirtyFlags = EDirty::NONE; }

	// Transform components
	Vector Pos = Vector::ZeroVector;
	Vector Rot = Vector::ZeroVector;
	Vector Scale = Vector::OneVector;

	// Hook for subclasses to react to transform dirties (e.g., raytracing updates)
	virtual void OnTransformDirty() {}
};
