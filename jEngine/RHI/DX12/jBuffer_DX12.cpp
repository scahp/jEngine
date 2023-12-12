#include "pch.h"
#include "jBuffer_DX12.h"

void jBuffer_DX12::Release()
{
    CBV.Free();
    SRV.Free();
    UAV.Free();
}
