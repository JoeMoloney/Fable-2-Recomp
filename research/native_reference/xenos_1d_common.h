// Local XenosRecomp extension for a distinct 1D descriptor lane.
// Existing descriptor offsets (2D=0, 3D=64, Cube=128, sampler=192) are retained.
// The 1D index lane begins at shared-constant byte offset 288; a runtime binding
// implementation must allocate and populate it before these shaders are usable.
// Keep this in a distinct descriptor set: t0 and s0 in the same HLSL space
// otherwise both lower to Vulkan set 3, binding 0.
Texture1D<float4> g_Texture1DDescriptorHeap[] : register(t0, space5);

float4 tfetch1D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float texCoord)
{
    return g_Texture1DDescriptorHeap[resourceDescriptorIndex].Sample(
        g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord);
}

float getWeights1D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float texCoord)
{
    Texture1D<float4> texture = g_Texture1DDescriptorHeap[resourceDescriptorIndex];
    uint width;
    texture.GetDimensions(width);
    return select(isnan(texCoord), 0.0, frac(texCoord * width - 0.5));
}
