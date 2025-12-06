#include "pch.h"
#include "jSceneObject.h"

jSceneObject::jSceneObject()
{
}

jSceneObject::~jSceneObject()
{
}

void jSceneObject::UpdateWorldMatrix()
{
	if (static_cast<int32>(DirtyFlags) & static_cast<int32>(EDirty::POS_ROT_SCALE))
	{
		auto posMatrix = Matrix::MakeTranslate(Pos);
		auto rotMatrix = Matrix::MakeRotate(Rot);
		auto scaleMatrix = Matrix::MakeScale(Scale);
		World = posMatrix * rotMatrix * scaleMatrix;

		ClearDirtyFlags(EDirty::POS_ROT_SCALE);
	}
}
