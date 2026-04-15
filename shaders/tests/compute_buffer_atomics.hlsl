#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle output_buffer;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryRWByteAddressBuffer buffer = LuminaryRWByteAddressBuffer::Create(constants.output_buffer);

    uint thread_id = dispatchThreadId.x + dispatchThreadId.y * 8;
    uint original;

    // Each thread group performs atomics on shared locations
    // Offset 0: atomic add (each thread adds 1)
    buffer.InterlockedAdd(0, 1u, original);

    // Offset 4: atomic min (track minimum value)
    buffer.InterlockedMin(4, thread_id, original);

    // Offset 8: atomic max (track maximum value)
    buffer.InterlockedMax(8, thread_id, original);

    // Offset 12: atomic compare-exchange
    uint expected = 0xDEADBEEFu;
    uint replacement = 0xCAFEBABEu;
    buffer.InterlockedCompareExchange(12, expected, replacement, original);
}
