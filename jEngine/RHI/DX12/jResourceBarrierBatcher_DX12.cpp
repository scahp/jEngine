#include "pch.h"
#include "jResourceBarrierBatcher_DX12.h"
#include "jTexture_DX12.h"
#include "jBuffer_DX12.h"

void jResourceBarrierBatcher_DX12::AddUAV(jBuffer* InBuffer)
{
    check(InBuffer);

    auto Buffer_DX12 = (jBuffer_DX12*)InBuffer;
    check(Buffer_DX12->Buffer);
    check(Buffer_DX12->Buffer->IsValid());

    AddUAV_Internal(Buffer_DX12->Buffer->Get());
}

void jResourceBarrierBatcher_DX12::AddUAV(jTexture* InTexture)
{
    check(InTexture);

    auto Texture_DX12 = (jTexture_DX12*)InTexture;
    check(Texture_DX12->Texture);
    check(Texture_DX12->Texture->IsValid());

    AddUAV_Internal(Texture_DX12->Texture->Get());
}

void jResourceBarrierBatcher_DX12::AddTransition(jBuffer* InBuffer, EResourceLayout InNewLayout)
{
    check(InBuffer);

    auto Buffer_DX12 = (jBuffer_DX12*)InBuffer;
    check(Buffer_DX12->Buffer);
    check(Buffer_DX12->Buffer->IsValid());

    const auto SrcLayout = GetDX12ResourceLayout(Buffer_DX12->Layout);
    const auto DstLayout = GetDX12ResourceLayout(InNewLayout);
    if (SrcLayout == DstLayout)
        return;

    AddTransition_Internal(Buffer_DX12->Buffer->Get(), SrcLayout, DstLayout);
}

void jResourceBarrierBatcher_DX12::AddTransition(jTexture* InTexture, EResourceLayout InNewLayout)
{
    check(InTexture);
    
    auto Texture_DX12 = (jTexture_DX12*)InTexture;
    check(Texture_DX12->Texture);
    check(Texture_DX12->Texture->IsValid());

    const auto SrcLayout = GetDX12ResourceLayout(Texture_DX12->Layout);
    const auto DstLayout = GetDX12ResourceLayout(InNewLayout);
    if (SrcLayout == DstLayout)
        return;

    AddTransition_Internal(Texture_DX12->Texture->Get(), SrcLayout, DstLayout);
}

void jResourceBarrierBatcher_DX12::Flush(jCommandBuffer* InCommandBuffer)
{

}
