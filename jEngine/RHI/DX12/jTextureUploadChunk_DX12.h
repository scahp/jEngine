#pragma once
#include <d3d12.h>

// The caller validates the offset-zero staging chunk and owns resource states
// and lifetimes. blockRows counts compressed block rows, not logical texel rows.
inline void CopyTextureUploadChunk_DX12(ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* destinationResource, UINT destinationSubresource, UINT destinationY,
    ID3D12Resource* stagingResource, DXGI_FORMAT format, UINT logicalWidth,
    UINT blockWidth, UINT blockHeight, UINT blockRows, UINT rowPitch)
{
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = destinationResource;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = destinationSubresource;

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = stagingResource;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint.Offset = 0;
    source.PlacedFootprint.Footprint.Format = format;
    source.PlacedFootprint.Footprint.Width = ((logicalWidth + blockWidth - 1) / blockWidth) * blockWidth;
    source.PlacedFootprint.Footprint.Height = blockRows * blockHeight;
    source.PlacedFootprint.Footprint.Depth = 1;
    source.PlacedFootprint.Footprint.RowPitch = rowPitch;

    // Copy the entire block-padded chunk. A logical 2x2 or 1x1 source box is
    // invalid for BC formats even though those are valid destination mip sizes.
    commandList->CopyTextureRegion(&destination, 0, destinationY, 0, &source, nullptr);
}
