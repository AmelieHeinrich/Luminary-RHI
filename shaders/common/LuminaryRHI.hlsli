#ifndef LUMINARY_RHI_HLSLI
#define LUMINARY_RHI_HLSLI

static const int LUMINARY_INVALID_DESCRIPTOR = -1;
typedef uint ResourceHandle;

#if LUMINARY_METAL || LUMINARY_D3D12
    #define LUMINARY_PUSH_CONSTANTS(type, name) ConstantBuffer<type> name : register(b0)
#elif LUMINARY_VULKAN
    #define LUMINARY_PUSH_CONSTANTS(type, name) [[vk::push_constant]] ConstantBuffer<type> name : register(b0)
    #if LUMINARY_HAS_RAYTRACING
        [[vk::binding(2, 0)]] RaytracingAccelerationStructure __lrhi_as_array[];
    #endif
#endif

// Samplers are defined first so texture types can use them in Sample* methods

class LuminarySampler
{
    ResourceHandle handle;
    SamplerState state;

    static LuminarySampler Create(ResourceHandle id)
    {
        LuminarySampler s;
        s.handle = id;
        s.state  = SamplerDescriptorHeap[id];
        return s;
    }

    ResourceHandle Handle() { return handle; }
    SamplerState   Resource() { return state; }
};

class LuminaryComparisonSampler
{
    ResourceHandle       handle;
    SamplerComparisonState state;

    static LuminaryComparisonSampler Create(ResourceHandle id)
    {
        LuminaryComparisonSampler s;
        s.handle = id;
        s.state  = SamplerDescriptorHeap[id];
        return s;
    }

    ResourceHandle         Handle() { return handle; }
    SamplerComparisonState Resource() { return state; }
};

// ---- Read-only textures ----

template<typename T>
class LuminaryTexture1D
{
    ResourceHandle handle;
    Texture1D<T>   texture;

    static LuminaryTexture1D<T> Create(ResourceHandle id)
    {
        LuminaryTexture1D<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle Handle()   { return handle; }
    Texture1D<T>   Resource() { return texture; }

    T    Load(int2 location)                                                         { return texture.Load(location); }
    T    Sample(LuminarySampler s, float location)                                   { return texture.Sample(s.state, location); }
    T    SampleLevel(LuminarySampler s, float location, float lod)                   { return texture.SampleLevel(s.state, location, lod); }
    T    SampleBias(LuminarySampler s, float location, float bias)                   { return texture.SampleBias(s.state, location, bias); }
    T    SampleGrad(LuminarySampler s, float location, float ddx, float ddy)         { return texture.SampleGrad(s.state, location, ddx, ddy); }
    void GetDimensions(uint mip, out uint width, out uint numLevels)                 { texture.GetDimensions(mip, width, numLevels); }
};

template<typename T>
class LuminaryTexture2D
{
    ResourceHandle handle;
    Texture2D<T>   texture;

    static LuminaryTexture2D<T> Create(ResourceHandle id)
    {
        LuminaryTexture2D<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle Handle()   { return handle; }
    Texture2D<T>   Resource() { return texture; }

    T     Load(int3 location)                                                        { return texture.Load(location); }
    T     Sample(LuminarySampler s, float2 uv)                                       { return texture.Sample(s.state, uv); }
    T     SampleLevel(LuminarySampler s, float2 uv, float lod)                       { return texture.SampleLevel(s.state, uv, lod); }
    T     SampleBias(LuminarySampler s, float2 uv, float bias)                       { return texture.SampleBias(s.state, uv, bias); }
    T     SampleGrad(LuminarySampler s, float2 uv, float2 ddx, float2 ddy)          { return texture.SampleGrad(s.state, uv, ddx, ddy); }
    float SampleCmp(LuminaryComparisonSampler s, float2 uv, float cmp)              { return texture.SampleCmp(s.state, uv, cmp); }
    float SampleCmpLevelZero(LuminaryComparisonSampler s, float2 uv, float cmp)     { return texture.SampleCmpLevelZero(s.state, uv, cmp); }
    void  GetDimensions(uint mip, out uint width, out uint height, out uint numLevels) { texture.GetDimensions(mip, width, height, numLevels); }
};

template<typename T>
class LuminaryTexture2DArray
{
    ResourceHandle    handle;
    Texture2DArray<T> texture;

    static LuminaryTexture2DArray<T> Create(ResourceHandle id)
    {
        LuminaryTexture2DArray<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle    Handle()   { return handle; }
    Texture2DArray<T> Resource() { return texture; }

    T    Load(int4 location)                                                         { return texture.Load(location); }
    T    Sample(LuminarySampler s, float3 uvw)                                       { return texture.Sample(s.state, uvw); }
    T    SampleLevel(LuminarySampler s, float3 uvw, float lod)                       { return texture.SampleLevel(s.state, uvw, lod); }
    T    SampleBias(LuminarySampler s, float3 uvw, float bias)                       { return texture.SampleBias(s.state, uvw, bias); }
    T    SampleGrad(LuminarySampler s, float3 uvw, float2 ddx, float2 ddy)          { return texture.SampleGrad(s.state, uvw, ddx, ddy); }
    void GetDimensions(uint mip, out uint width, out uint height, out uint elements, out uint numLevels) { texture.GetDimensions(mip, width, height, elements, numLevels); }
};

template<typename T>
class LuminaryTexture3D
{
    ResourceHandle handle;
    Texture3D<T>   texture;

    static LuminaryTexture3D<T> Create(ResourceHandle id)
    {
        LuminaryTexture3D<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle Handle()   { return handle; }
    Texture3D<T>   Resource() { return texture; }

    T    Load(int4 location)                                                         { return texture.Load(location); }
    T    Sample(LuminarySampler s, float3 uvw)                                       { return texture.Sample(s.state, uvw); }
    T    SampleLevel(LuminarySampler s, float3 uvw, float lod)                       { return texture.SampleLevel(s.state, uvw, lod); }
    T    SampleBias(LuminarySampler s, float3 uvw, float bias)                       { return texture.SampleBias(s.state, uvw, bias); }
    T    SampleGrad(LuminarySampler s, float3 uvw, float3 ddx, float3 ddy)          { return texture.SampleGrad(s.state, uvw, ddx, ddy); }
    void GetDimensions(uint mip, out uint width, out uint height, out uint depth, out uint numLevels) { texture.GetDimensions(mip, width, height, depth, numLevels); }
};

template<typename T>
class LuminaryTextureCube
{
    ResourceHandle handle;
    TextureCube<T> texture;

    static LuminaryTextureCube<T> Create(ResourceHandle id)
    {
        LuminaryTextureCube<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle Handle()   { return handle; }
    TextureCube<T> Resource() { return texture; }

    T    Sample(LuminarySampler s, float3 dir)                                       { return texture.Sample(s.state, dir); }
    T    SampleLevel(LuminarySampler s, float3 dir, float lod)                       { return texture.SampleLevel(s.state, dir, lod); }
    T    SampleBias(LuminarySampler s, float3 dir, float bias)                       { return texture.SampleBias(s.state, dir, bias); }
    T    SampleGrad(LuminarySampler s, float3 dir, float3 ddx, float3 ddy)          { return texture.SampleGrad(s.state, dir, ddx, ddy); }
    void GetDimensions(uint mip, out uint width, out uint height, out uint numLevels) { texture.GetDimensions(mip, width, height, numLevels); }
};

// ---- Read-write textures ----

template<typename T>
class LuminaryRWTexture1D
{
    ResourceHandle handle;
    RWTexture1D<T> texture;

    static LuminaryRWTexture1D<T> Create(ResourceHandle id)
    {
        LuminaryRWTexture1D<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle Handle()   { return handle; }
    RWTexture1D<T> Resource() { return texture; }

    T    Load(uint index)                { return texture[index]; }
    void Store(uint index, T value)      { texture[index] = value; }
    void GetDimensions(out uint width)   { texture.GetDimensions(width); }
};

template<typename T>
class LuminaryRWTexture2D
{
    ResourceHandle handle;
    RWTexture2D<T> texture;

    static LuminaryRWTexture2D<T> Create(ResourceHandle id)
    {
        LuminaryRWTexture2D<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle Handle()   { return handle; }
    RWTexture2D<T> Resource() { return texture; }

    T    Load(uint2 coords)              { return texture[coords]; }
    void Store(uint2 coords, T value)    { texture[coords] = value; }
    void GetDimensions(out uint width, out uint height) { texture.GetDimensions(width, height); }
};

template<typename T>
class LuminaryRWTexture2DArray
{
    ResourceHandle      handle;
    RWTexture2DArray<T> texture;

    static LuminaryRWTexture2DArray<T> Create(ResourceHandle id)
    {
        LuminaryRWTexture2DArray<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle      Handle()   { return handle; }
    RWTexture2DArray<T> Resource() { return texture; }

    T    Load(uint3 coords)              { return texture[coords]; }
    void Store(uint3 coords, T value)    { texture[coords] = value; }
    void GetDimensions(out uint width, out uint height, out uint elements) { texture.GetDimensions(width, height, elements); }
};

template<typename T>
class LuminaryRWTexture3D
{
    ResourceHandle handle;
    RWTexture3D<T> texture;

    static LuminaryRWTexture3D<T> Create(ResourceHandle id)
    {
        LuminaryRWTexture3D<T> t;
        t.handle  = id;
        t.texture = ResourceDescriptorHeap[id];
        return t;
    }

    ResourceHandle Handle()   { return handle; }
    RWTexture3D<T> Resource() { return texture; }

    T    Load(uint3 coords)              { return texture[coords]; }
    void Store(uint3 coords, T value)    { texture[coords] = value; }
    void GetDimensions(out uint width, out uint height, out uint depth) { texture.GetDimensions(width, height, depth); }
};

// ---- Buffers ----

template<typename T>
class LuminaryStructuredBuffer
{
    ResourceHandle      handle;
    StructuredBuffer<T> buffer;

    static LuminaryStructuredBuffer<T> Create(ResourceHandle id)
    {
        LuminaryStructuredBuffer<T> b;
        b.handle = id;
        b.buffer = ResourceDescriptorHeap[id];
        return b;
    }

    ResourceHandle      Handle()   { return handle; }
    StructuredBuffer<T> Resource() { return buffer; }

    T    Load(int index)                                               { return buffer[index]; }
    void GetDimensions(out uint numStructs, out uint stride)           { buffer.GetDimensions(numStructs, stride); }
};

template<typename T>
class LuminaryRWStructuredBuffer
{
    ResourceHandle        handle;
    RWStructuredBuffer<T> buffer;

    static LuminaryRWStructuredBuffer<T> Create(ResourceHandle id)
    {
        LuminaryRWStructuredBuffer<T> b;
        b.handle = id;
        b.buffer = ResourceDescriptorHeap[id];
        return b;
    }

    ResourceHandle        Handle()   { return handle; }
    RWStructuredBuffer<T> Resource() { return buffer; }

    T    Load(int index)                                               { return buffer[index]; }
    void Store(int index, T value)                                     { buffer[index] = value; }
    void GetDimensions(out uint numStructs, out uint stride)           { buffer.GetDimensions(numStructs, stride); }
};

class LuminaryByteAddressBuffer
{
    ResourceHandle    handle;
    ByteAddressBuffer buffer;

    static LuminaryByteAddressBuffer Create(ResourceHandle id)
    {
        LuminaryByteAddressBuffer b;
        b.handle = id;
        b.buffer = ResourceDescriptorHeap[id];
        return b;
    }

    ResourceHandle    Handle()   { return handle; }
    ByteAddressBuffer Resource() { return buffer; }

    uint  Load(uint addr)  { return buffer.Load(addr); }
    uint2 Load2(uint addr) { return buffer.Load2(addr); }
    uint3 Load3(uint addr) { return buffer.Load3(addr); }
    uint4 Load4(uint addr) { return buffer.Load4(addr); }
};

class LuminaryRWByteAddressBuffer
{
    ResourceHandle      handle;
    RWByteAddressBuffer buffer;

    static LuminaryRWByteAddressBuffer Create(ResourceHandle id)
    {
        LuminaryRWByteAddressBuffer b;
        b.handle = id;
        b.buffer = ResourceDescriptorHeap[id];
        return b;
    }

    ResourceHandle      Handle()   { return handle; }
    RWByteAddressBuffer Resource() { return buffer; }

    uint  Load(uint addr)   { return buffer.Load(addr); }
    uint2 Load2(uint addr)  { return buffer.Load2(addr); }
    uint3 Load3(uint addr)  { return buffer.Load3(addr); }
    uint4 Load4(uint addr)  { return buffer.Load4(addr); }

    void Store(uint addr, uint v)   { buffer.Store(addr, v); }
    void Store2(uint addr, uint2 v) { buffer.Store2(addr, v); }
    void Store3(uint addr, uint3 v) { buffer.Store3(addr, v); }
    void Store4(uint addr, uint4 v) { buffer.Store4(addr, v); }

    void InterlockedAdd(uint addr, uint v, out uint original)                           { buffer.InterlockedAdd(addr, v, original); }
    void InterlockedMin(uint addr, uint v, out uint original)                           { buffer.InterlockedMin(addr, v, original); }
    void InterlockedMax(uint addr, uint v, out uint original)                           { buffer.InterlockedMax(addr, v, original); }
    void InterlockedAnd(uint addr, uint v, out uint original)                           { buffer.InterlockedAnd(addr, v, original); }
    void InterlockedOr(uint addr, uint v, out uint original)                            { buffer.InterlockedOr(addr, v, original); }
    void InterlockedXor(uint addr, uint v, out uint original)                           { buffer.InterlockedXor(addr, v, original); }
    void InterlockedExchange(uint addr, uint v, out uint original)                      { buffer.InterlockedExchange(addr, v, original); }
    void InterlockedCompareExchange(uint addr, uint compare, uint v, out uint original) { buffer.InterlockedCompareExchange(addr, compare, v, original); }
};

// ---- Raytracing ----

class LuminaryAccelerationStructure
{
    ResourceHandle handle;

    static LuminaryAccelerationStructure Create(ResourceHandle id)
    {
        LuminaryAccelerationStructure a;
        a.handle = id;
        return a;
    }

    ResourceHandle Handle() { return handle; }

    RaytracingAccelerationStructure Resource()
    {
#if LUMINARY_VULKAN && LUMINARY_HAS_RAYTRACING
        return __lrhi_as_array[handle];
#else
        return ResourceDescriptorHeap[handle];
#endif
    }
};

// ---- Draw ID ----

struct __luminary_draw_id { uint id; };

#if !LUMINARY_VULKAN
    #define LUMINARY_DECLARE_DRAW_ID() ConstantBuffer<__luminary_draw_id> __luminary_draw_id_binding : register(b1)
    #define LUMINARY_DRAW_ID() __luminary_draw_id_binding.id
#else
    #define LUMINARY_DECLARE_DRAW_ID() [[vk::ext_builtin_input(4426)]] static const uint __luminary_draw_id_var;
    #define LUMINARY_DRAW_ID() __luminary_draw_id_var
#endif

#endif // LUMINARY_RHI_HLSLI
