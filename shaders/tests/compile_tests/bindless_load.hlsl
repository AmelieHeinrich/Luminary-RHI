#include "shaders/common/LuminaryRHI.hlsli"

struct PushConstants
{
    ResourceHandle tex1d;
    ResourceHandle tex2d;
    ResourceHandle tex2dArray;
    ResourceHandle tex3d;
    ResourceHandle texCube;
    ResourceHandle rwTex1d;
    ResourceHandle rwTex2d;
    ResourceHandle rwTex2dArray;
    ResourceHandle rwTex3d;

    ResourceHandle buffer;
    ResourceHandle rwBuffer;
    ResourceHandle byteAddressBuffer;
    ResourceHandle rwByteAddressBuffer;

    ResourceHandle accelerationStructure;
    ResourceHandle sampler;
    ResourceHandle comparisonSampler;
};
LUMINARY_PUSH_CONSTANTS(PushConstants, push);

[numthreads(1, 1, 1)]
void main()
{
    LuminaryTexture1D<float4>        t1d      = LuminaryTexture1D<float4>::Create(push.tex1d);
    LuminaryTexture2D<float4>        t2d      = LuminaryTexture2D<float4>::Create(push.tex2d);
    LuminaryTexture2DArray<float4>   t2dArray = LuminaryTexture2DArray<float4>::Create(push.tex2dArray);
    LuminaryTexture3D<float4>        t3d      = LuminaryTexture3D<float4>::Create(push.tex3d);
    LuminaryTextureCube<float4>      tCube    = LuminaryTextureCube<float4>::Create(push.texCube);
    LuminaryRWTexture1D<float4>      rwT1d    = LuminaryRWTexture1D<float4>::Create(push.rwTex1d);
    LuminaryRWTexture2D<float4>      rwT2d    = LuminaryRWTexture2D<float4>::Create(push.rwTex2d);
    LuminaryRWTexture2DArray<float4> rwT2dArray = LuminaryRWTexture2DArray<float4>::Create(push.rwTex2dArray);
    LuminaryRWTexture3D<float4>      rwT3d    = LuminaryRWTexture3D<float4>::Create(push.rwTex3d);
    LuminaryStructuredBuffer<float4>   buf    = LuminaryStructuredBuffer<float4>::Create(push.buffer);
    LuminaryRWStructuredBuffer<float4> rwBuf  = LuminaryRWStructuredBuffer<float4>::Create(push.rwBuffer);
    LuminaryByteAddressBuffer   bab    = LuminaryByteAddressBuffer::Create(push.byteAddressBuffer);
    LuminaryRWByteAddressBuffer rwBab  = LuminaryRWByteAddressBuffer::Create(push.rwByteAddressBuffer);
    LuminaryAccelerationStructure as   = LuminaryAccelerationStructure::Create(push.accelerationStructure);
    LuminarySampler           samp     = LuminarySampler::Create(push.sampler);
    LuminaryComparisonSampler cmpSamp  = LuminaryComparisonSampler::Create(push.comparisonSampler);
}
