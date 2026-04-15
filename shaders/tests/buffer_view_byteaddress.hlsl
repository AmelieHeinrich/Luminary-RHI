#include "shaders/common/LuminaryRHI.hlsli"

struct MyConstants {
    ResourceHandle input_buffer;
    ResourceHandle output_buffer;
};
LUMINARY_PUSH_CONSTANTS(MyConstants, constants);

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryByteAddressBuffer in_buf = LuminaryByteAddressBuffer::Create(constants.input_buffer);
    LuminaryRWByteAddressBuffer out_buf = LuminaryRWByteAddressBuffer::Create(constants.output_buffer);

    // Read using Load, Load2, Load4 and copy to output
    uint  v0 = in_buf.Load(0);
    uint2 v1 = in_buf.Load2(4);
    uint4 v2 = in_buf.Load4(12);

    out_buf.Store(0, v0);
    out_buf.Store2(4, v1);
    out_buf.Store4(12, v2);
}
