#include "shaders/common/LuminaryRHI.hlsli"

struct MyConstants {
    ResourceHandle rw_buffer;
    ResourceHandle output_buffer;
    uint pass_index;
};
LUMINARY_PUSH_CONSTANTS(MyConstants, constants);

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (constants.pass_index == 0) {
        // Pass A: write a pattern using Store, Store2, Store4
        LuminaryRWByteAddressBuffer buf = LuminaryRWByteAddressBuffer::Create(constants.rw_buffer);
        buf.Store(0,  0xDEADBEEFu);
        buf.Store2(4,  uint2(0x11223344u, 0xAABBCCDDu));
        buf.Store4(12, uint4(1u, 2u, 3u, 4u));
    } else {
        // Pass B: read back and copy to output using Load, Load2, Load4
        LuminaryRWByteAddressBuffer buf = LuminaryRWByteAddressBuffer::Create(constants.rw_buffer);
        LuminaryRWByteAddressBuffer out_buf = LuminaryRWByteAddressBuffer::Create(constants.output_buffer);

        uint  v0 = buf.Load(0);
        uint2 v1 = buf.Load2(4);
        uint4 v2 = buf.Load4(12);

        out_buf.Store(0,  v0);
        out_buf.Store2(4,  v1);
        out_buf.Store4(12, v2);
    }
}
