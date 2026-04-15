#include "shaders/common/LuminaryRHI.hlsli"

struct MyConstants {
    ResourceHandle input_buffer;
    ResourceHandle output_buffer;
};
LUMINARY_PUSH_CONSTANTS(MyConstants, constants);

struct ConstantData {
    uint x;
    uint y;
    uint z;
    uint w;
};

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<ConstantData> cb = ResourceDescriptorHeap[constants.input_buffer];
    LuminaryRWByteAddressBuffer out_buf = LuminaryRWByteAddressBuffer::Create(constants.output_buffer);

    out_buf.Store4(0, uint4(cb.x, cb.y, cb.z, cb.w));
}
