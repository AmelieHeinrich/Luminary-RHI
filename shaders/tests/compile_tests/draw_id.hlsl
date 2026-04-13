#include "shaders/common/LuminaryRHI.hlsli"

LUMINARY_DECLARE_DRAW_ID();

[numthreads(1, 1, 1)]
void main()
{
    uint test = LUMINARY_DRAW_ID();
}
