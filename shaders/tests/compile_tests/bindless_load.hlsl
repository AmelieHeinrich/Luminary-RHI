#include "shaders/common/LuminaryRHI.hlsli"

struct PushConstants
{
    LuminaryTexture1D<float4> tex1d;
    LuminaryTexture2D<float4> tex2d;
    LuminaryTexture2DArray<float4> tex2dArray;
    LuminaryTexture3D<float4> tex3d;
    LuminaryTextureCube<float4> texCube;
    LuminaryRWTexture1D<float4> rwTex1d;
    LuminaryRWTexture2D<float4> rwTex2d;
    LuminaryRWTexture2DArray<float4> rwTex2dArray;
    LuminaryRWTexture3D<float4> rwTex3d;

    LuminaryStructuredBuffer<float4> buffer;
    LuminaryRWStructuredBuffer<float4> rwBuffer;
    LuminaryByteAddressBuffer byteAddressBuffer;
    LuminaryRWByteAddressBuffer rwByteAddressBuffer;

    LuminaryAccelerationStructure accelerationStructure;
    LuminarySampler sampler;
    LuminaryComparisonSampler comparisonSampler;
};
LUMINARY_PUSH_CONSTANTS(PushConstants, push);

[numthreads(1, 1, 1)]
void main()
{
    Texture1D<float4> t1d = push.tex1d.Load();
    Texture2D<float4> t2d = push.tex2d.Load();
    Texture2DArray<float4> t2dArray = push.tex2dArray.Load();
    Texture3D<float4> t3d = push.tex3d.Load();
    TextureCube<float4> tCube = push.texCube.Load();
    RWTexture1D<float4> rwT1d = push.rwTex1d.Load();
    RWTexture2D<float4> rwT2d = push.rwTex2d.Load();
    RWTexture2DArray<float4> rwT2dArray = push.rwTex2dArray.Load();
    RWTexture3D<float4> rwT3d = push.rwTex3d.Load();
    StructuredBuffer<float4> buffer = push.buffer.Load();
    RWStructuredBuffer<float4> rwBuffer = push.rwBuffer.Load();
    ByteAddressBuffer byteAddressBuffer = push.byteAddressBuffer.Load();
    RWByteAddressBuffer rwByteAddressBuffer = push.rwByteAddressBuffer.Load();
    RaytracingAccelerationStructure accelerationStructure = push.accelerationStructure.Load();
    SamplerState sampler = push.sampler.Load();
    SamplerComparisonState comparisonSampler = push.comparisonSampler.Load();
}
