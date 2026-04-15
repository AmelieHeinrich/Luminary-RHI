#include "shaders/common/LuminaryRHI.hlsli"

struct Constants {
    ResourceHandle buffer;
    uint pass_index;  // 0 for stage 1, 1 for stage 2, 2 for stage 3
};
LUMINARY_PUSH_CONSTANTS(Constants, constants);

[numthreads(8, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    LuminaryRWStructuredBuffer<uint> buf = LuminaryRWStructuredBuffer<uint>::Create(constants.buffer);
    uint idx = dispatchThreadId.x;

    if (constants.pass_index == 0) {
        // Stage 1: Write initial values
        buf.Store(idx, idx * 100);
    } else if (constants.pass_index == 1) {
        // Stage 2: Read stage 1 values and multiply
        uint val = buf.Load(idx);
        buf.Store(idx, val * 2);
    } else {
        // Stage 3: Read stage 2 values and add verification marker
        uint val = buf.Load(idx);
        buf.Store(idx, val + 1000000);  // Add marker to indicate stage 3 completed
    }
}
