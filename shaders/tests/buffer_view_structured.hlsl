#include "shaders/common/LuminaryRHI.hlsli"

struct MyConstants {
    ResourceHandle input_buffer;
    ResourceHandle output_buffer;
};
LUMINARY_PUSH_CONSTANTS(MyConstants, constants);

struct Element {
    uint x;
    uint y;
};

[numthreads(4, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryStructuredBuffer<Element> in_buf = LuminaryStructuredBuffer<Element>::Create(constants.input_buffer);
    LuminaryRWByteAddressBuffer out_buf = LuminaryRWByteAddressBuffer::Create(constants.output_buffer);

    uint idx = dispatchThreadId.x;
    Element e = in_buf.Load(idx);
    out_buf.Store2(idx * 8, uint2(e.x, e.y));
}
