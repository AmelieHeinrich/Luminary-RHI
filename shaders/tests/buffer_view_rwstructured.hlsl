#include "shaders/common/LuminaryRHI.hlsli"

struct MyConstants {
    ResourceHandle rw_struct_buffer;
    ResourceHandle output_buffer;
    uint pass_index;
};
LUMINARY_PUSH_CONSTANTS(MyConstants, constants);

struct Element {
    uint x;
    uint y;
};

[numthreads(4, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint idx = dispatchThreadId.x;

    if (constants.pass_index == 0) {
        // Pass A: write a pattern into the RWStructuredBuffer
        LuminaryRWStructuredBuffer<Element> buf = LuminaryRWStructuredBuffer<Element>::Create(constants.rw_struct_buffer);
        Element e;
        e.x = idx * 2u;
        e.y = idx * 2u + 1u;
        buf.Store(idx, e);
    } else {
        // Pass B: read from RWStructuredBuffer, copy to output
        LuminaryRWStructuredBuffer<Element> buf = LuminaryRWStructuredBuffer<Element>::Create(constants.rw_struct_buffer);
        LuminaryRWByteAddressBuffer out_buf = LuminaryRWByteAddressBuffer::Create(constants.output_buffer);
        Element e = buf.Load(idx);
        out_buf.Store2(idx * 8u, uint2(e.x, e.y));
    }
}
