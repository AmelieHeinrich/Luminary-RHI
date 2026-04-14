#include "shaders/common/LuminaryRHI.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

[NumThreads(1, 1, 1)]
[OutputTopology("triangle")]
void MSMain(uint gid: SV_GroupID,
            uint gtid: SV_GroupThreadID,
            out vertices VSOutput OutVerts[3],
            out indices uint3 OutTris[1])
{
    // Write single triangle
    SetMeshOutputCounts(3, 1);

    OutVerts[0].position = float4(0.0f, 0.5f, 0.0f, 1.0f);
    OutVerts[0].color = float3(1.0f, 0.0f, 0.0f);
    OutVerts[1].position = float4(0.5f, -0.5f, 0.0f, 1.0f);
    OutVerts[1].color = float3(0.0f, 1.0f, 0.0f);
    OutVerts[2].position = float4(-0.5f, -0.5f, 0.0f, 1.0f);
    OutVerts[2].color = float3(0.0f, 0.0f, 1.0f);
    OutTris[0] = uint3(0, 1, 2);
}

float4 PSMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0f);
}