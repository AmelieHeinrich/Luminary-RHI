#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    LuminaryRWByteAddressBuffer output_buffer;
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    RWByteAddressBuffer buffer = constants.output_buffer.Load();
    
    uint thread_id = dispatchThreadId.x + dispatchThreadId.y * 8;
    
    // Each thread group performs atomics on shared locations
    // Offset 0: atomic add (each thread adds 1)
    InterlockedAdd(buffer.Load<uint>(0), 1u);
    
    // Offset 4: atomic min (track minimum value)
    InterlockedMin(buffer.Load<uint>(4), thread_id);
    
    // Offset 8: atomic max (track maximum value)
    InterlockedMax(buffer.Load<uint>(8), thread_id);
    
    // Offset 12: atomic compare-exchange
    uint expected = 0xDEADBEEFu;
    uint replacement = 0xCAFEBABEu;
    InterlockedCompareExchange(buffer.Load<uint>(12), expected, replacement, replacement);
}
