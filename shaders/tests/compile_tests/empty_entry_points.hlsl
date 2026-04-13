#include "shaders/common/LuminaryRHI.hlsli"

struct VSOutput
{
    float4 position : SV_Position;
};

struct DummyPayLoad
{ 
    uint dummyData; 
}; 

groupshared DummyPayLoad payload;

float4 VSMain(uint vertexId : SV_VertexID) : SV_Position
{
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

float4 PSMain() : SV_Target
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
}

[numthreads(1, 1, 1)]
void ASMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    DispatchMesh(1, 1, 1, payload);
}

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
    OutVerts[1].position = float4(0.5f, -0.5f, 0.0f, 1.0f);
    OutVerts[2].position = float4(-0.5f, -0.5f, 0.0f, 1.0f);
    OutTris[0] = uint3(0, 1, 2);
}
