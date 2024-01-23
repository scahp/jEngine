#include "pch.h"
#include "jRaytracingScene.h"
#include "jRHIType.h"

jRaytracingScene::~jRaytracingScene()
{
    InstanceList.clear();
    TLASBufferPtr.reset();
    ScratchTLASBufferPtr.reset();
    InstanceUploadBufferPtr.reset();
    RaytracingOutputPtr.reset();
}
