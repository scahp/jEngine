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
	if (WorldMatrixOverride)
		return;
	if (static_cast<int32>(DirtyFlags) & static_cast<int32>(EDirty::POS_ROT_SCALE))
	{
		auto posMatrix = Matrix::MakeTranslate(Pos);
		auto rotMatrix = Matrix::MakeRotate(Rot);
		auto scaleMatrix = Matrix::MakeScale(Scale);
		World = posMatrix * rotMatrix * scaleMatrix;

		ClearDirtyFlags(EDirty::POS_ROT_SCALE);
	}
}

void jSceneObject::SetWorldMatrixOverride(const Matrix& matrix)
{
    if (WorldMatrixOverride && std::memcmp(&World, &matrix, sizeof(Matrix)) == 0)
        return;
    World = matrix;
    WorldMatrixOverride = true;
    ClearDirtyFlags();
    OnTransformDirty();
}

void jSceneObject::ClearWorldMatrixOverride()
{
    if (!WorldMatrixOverride)
        return;
    WorldMatrixOverride = false;
    SetDirtyFlags(EDirty::POS_ROT_SCALE);
}
