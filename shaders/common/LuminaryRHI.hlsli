#ifndef LUMINARY_RHI_HLSLI
#define LUMINARY_RHI_HLSLI

static const int LUMINARY_INVALID_DESCRIPTOR = -1;

#if LUMINARY_METAL
    #define LUMINARY_PUSH_CONSTANTS(type, name) ConstantBuffer<type> name : register(b0)
#elif LUMINARY_VULKAN
    #define LUMINARY_PUSH_CONSTANTS(type, name) [[vk::push_constant]] ConstantBuffer<type> name : register(b0)
    #if LUMINARY_HAS_RAYTRACING
        [[vk::binding(2, 0)]] RaytracingAccelerationStructure __lrhi_as_array[];
    #endif
#endif

// Texture types

template<typename T>
class LuminaryTexture1D
{
    uint bindless_index;

    Texture1D<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryTexture2D
{
    uint bindless_index;

    Texture2D<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryTexture2DArray
{
    uint bindless_index;

    Texture2DArray<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryTexture3D
{
    uint bindless_index;

    Texture3D<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryTextureCube
{
    uint bindless_index;

    TextureCube<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

// Buffer

template<typename T>
class LuminaryStructuredBuffer
{
    uint bindless_index;

    StructuredBuffer<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryRWStructuredBuffer
{
    uint bindless_index;

    RWStructuredBuffer<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

class LuminaryByteAddressBuffer
{
    uint bindless_index;

    ByteAddressBuffer Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

class LuminaryRWByteAddressBuffer
{
    uint bindless_index;

    RWByteAddressBuffer Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

// RW

template<typename T>
class LuminaryRWTexture1D
{
    uint bindless_index;

    RWTexture1D<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryRWTexture2D
{
    uint bindless_index;

    RWTexture2D<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryRWTexture2DArray
{
    uint bindless_index;

    RWTexture2DArray<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

template<typename T>
class LuminaryRWTexture3D
{
    uint bindless_index;

    RWTexture3D<T> Load()
    {
        return ResourceDescriptorHeap[bindless_index];
    }
};

class LuminarySampler
{
    uint bindless_index;

    SamplerState Load()
    {
        return SamplerDescriptorHeap[bindless_index];
    }
};

class LuminaryComparisonSampler
{
    uint bindless_index;

    SamplerComparisonState Load()
    {
        return SamplerDescriptorHeap[bindless_index];
    }
};

class LuminaryAccelerationStructure
{
    uint bindless_index;

    RaytracingAccelerationStructure Load()
    {
#if LUMINARY_VULKAN && LUMINARY_HAS_RAYTRACING
        return __lrhi_as_array[bindless_index];
#else
        return ResourceDescriptorHeap[bindless_index];
#endif
    }
};

struct __luminary_draw_id { uint id; };

#if !LUMINARY_VULKAN
    #define LUMINARY_DECLARE_DRAW_ID() ConstantBuffer<__luminary_draw_id> __luminary_draw_id_binding : register(b1)
    #define LUMINARY_DRAW_ID() __luminary_draw_id_binding.id
#else
    #define LUMINARY_DECLARE_DRAW_ID() [[vk::ext_builtin_input(4426)]] static const uint __luminary_draw_id_var;
    #define LUMINARY_DRAW_ID() __luminary_draw_id_var
#endif

#endif // LUMINARY_RHI_HLSLI
