#include "pch.h"
#include "jTexture_DX12.h"

jTexture_DX12::~jTexture_DX12()
{
    Release();
}

void jTexture_DX12::Release()
{
    SRV.Free();
    UAV.Free();
    RTV.Free();
    DSV.Free();
}
