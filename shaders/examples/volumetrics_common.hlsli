#ifndef VOLUMETRICS_COMMON_HLSLI
#define VOLUMETRICS_COMMON_HLSLI

static float hash13(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 31.32f);
    return frac((p.x + p.y) * p.z);
}

static float worley3d(float3 p)
{
    float3 cell = floor(p);
    float3 local = frac(p);
    float min_dist = 1.0e9f;

    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                float3 n = float3((float)x, (float)y, (float)z);
                float3 base_cell = cell + n;
                float3 jitter = float3(
                    hash13(base_cell + float3(1.0f, 0.0f, 0.0f)),
                    hash13(base_cell + float3(0.0f, 1.0f, 0.0f)),
                    hash13(base_cell + float3(0.0f, 0.0f, 1.0f)));
                float3 feature = n + jitter;
                float d = length(feature - local);
                min_dist = min(min_dist, d);
            }
        }
    }

    return min_dist;
}

#endif // VOLUMETRICS_COMMON_HLSLI
