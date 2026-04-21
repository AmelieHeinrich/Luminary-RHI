// dear imgui: Renderer Backend for Luminary RHI

#include "imgui_impl_luminary.h"

#ifndef IMGUI_DISABLE

#include "luminary_shader_compiler.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct ImGui_ImplLuminary_Texture
{
    LRHITexture texture;
    LRHITextureView view;
};

struct ImGui_ImplLuminary_FrameRenderBuffers
{
    LRHIBuffer vertex_buffer;
    LRHIBufferView vertex_buffer_view;
    int vertex_buffer_size;

    LRHIBuffer index_buffer;
    int index_buffer_size;

    ImGui_ImplLuminary_FrameRenderBuffers()
    {
        memset((void*)this, 0, sizeof(*this));
    }
};

struct ImGui_ImplLuminary_Data
{
    ImGui_ImplLuminary_InitInfo init_info;
    int frame_index;

    LRHIRenderPipeline pipeline;
    LRHIShaderModule vertex_shader;
    LRHIShaderModule fragment_shader;

    LRHISampler sampler;
    uint32_t sampler_bindless_index;

    ImGui_ImplLuminary_FrameRenderBuffers* frame_render_buffers;

    ImGui_ImplLuminary_Data()
    {
        memset((void*)this, 0, sizeof(*this));
        frame_index = -1;
    }
};

struct ImGui_ImplLuminary_PushConstants
{
    float projection[4][4];
    uint32_t vertex_buffer_index;
    uint32_t sampler_index;
    uint32_t texture_index;
    uint32_t vertex_offset;
};

struct ImGui_ImplLuminary_Vertex
{
    float pos[2];
    float uv[2];
    uint32_t col;
};

static const char* ImGui_ImplLuminary_ShaderSource = R"(
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

#endif // LUMINARY_RHI_HLSLI

struct ImGuiVertex
{
    float2 pos;
    float2 uv;
    uint col;
};

struct PushConstants
{
    float4x4 projection;
    ResourceHandle vertex_buffer;
    ResourceHandle sampler;
    ResourceHandle texture;
    uint vertex_offset;
};
LUMINARY_PUSH_CONSTANTS(PushConstants, push);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    LuminaryStructuredBuffer<ImGuiVertex> vb = LuminaryStructuredBuffer<ImGuiVertex>::Create(push.vertex_buffer);
    ImGuiVertex v = vb.Load(int(vertex_id + push.vertex_offset));

    VSOutput o;
    o.position = mul(push.projection, float4(v.pos, 0.0f, 1.0f));
    o.uv = v.uv;
    o.color = float4(
        float(v.col & 0xFFu),
        float((v.col >> 8) & 0xFFu),
        float((v.col >> 16) & 0xFFu),
        float((v.col >> 24) & 0xFFu)) / 255.0f;
    return o;
}

float4 PSMain(VSOutput i) : SV_Target
{
    LuminaryTexture2D<float4> tex = LuminaryTexture2D<float4>::Create(push.texture);
    LuminarySampler sampler = LuminarySampler::Create(push.sampler);
    return i.color * tex.Sample(sampler, i.uv);
}
)";

static ImGui_ImplLuminary_Data* ImGui_ImplLuminary_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplLuminary_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

static void ImGui_ImplLuminary_PrintError(const char* scope, const LRHIError* err)
{
    if (err && err->severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        printf("[imgui_impl_luminary] %s: %s\n", scope, err->message);
}

static bool ImGui_ImplLuminary_ResidencySetAddTextureAndUpdate(ImGui_ImplLuminary_Data* bd, LRHITexture texture)
{
    if (!bd || !bd->init_info.residency_set || !texture)
        return true;

    LRHIError err = {};
    lrhi_residency_set_add_texture(bd->init_info.residency_set, texture, &err);
    ImGui_ImplLuminary_PrintError("residency_set_add_texture", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        return false;

    err = {};
    lrhi_residency_set_update(bd->init_info.residency_set, &err);
    ImGui_ImplLuminary_PrintError("residency_set_update_texture", &err);
    return err.severity != LUMINARY_RHI_ERROR_SEVERITY_ERROR;
}

static bool ImGui_ImplLuminary_ResidencySetAddBufferAndUpdate(ImGui_ImplLuminary_Data* bd, LRHIBuffer buffer)
{
    if (!bd || !bd->init_info.residency_set || !buffer)
        return true;

    LRHIError err = {};
    lrhi_residency_set_add_buffer(bd->init_info.residency_set, buffer, &err);
    ImGui_ImplLuminary_PrintError("residency_set_add_buffer", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        return false;

    err = {};
    lrhi_residency_set_update(bd->init_info.residency_set, &err);
    ImGui_ImplLuminary_PrintError("residency_set_update_buffer", &err);
    return err.severity != LUMINARY_RHI_ERROR_SEVERITY_ERROR;
}

static void ImGui_ImplLuminary_ResidencySetRemoveTextureAndUpdate(ImGui_ImplLuminary_Data* bd, LRHITexture texture)
{
    if (!bd || !bd->init_info.residency_set || !texture)
        return;

    LRHIError err = {};
    lrhi_residency_set_remove_texture(bd->init_info.residency_set, texture, &err);
    ImGui_ImplLuminary_PrintError("residency_set_remove_texture", &err);
    err = {};
    lrhi_residency_set_update(bd->init_info.residency_set, &err);
    ImGui_ImplLuminary_PrintError("residency_set_update_remove_texture", &err);
}

static void ImGui_ImplLuminary_ResidencySetRemoveBufferAndUpdate(ImGui_ImplLuminary_Data* bd, LRHIBuffer buffer)
{
    if (!bd || !bd->init_info.residency_set || !buffer)
        return;

    LRHIError err = {};
    lrhi_residency_set_remove_buffer(bd->init_info.residency_set, buffer, &err);
    ImGui_ImplLuminary_PrintError("residency_set_remove_buffer", &err);
    err = {};
    lrhi_residency_set_update(bd->init_info.residency_set, &err);
    ImGui_ImplLuminary_PrintError("residency_set_update_remove_buffer", &err);
}

static ImTextureID ImGui_ImplLuminary_IndexToTexID(uint32_t index)
{
    const uintptr_t tag = (uintptr_t)1 << ((sizeof(uintptr_t) * 8) - 1);
    return (ImTextureID)((uintptr_t)index + 1u | tag);
}

static uint32_t ImGui_ImplLuminary_TexIDToIndex(ImTextureID tex_id)
{
    const uintptr_t raw = (uintptr_t)tex_id;
    const uintptr_t tag = (uintptr_t)1 << ((sizeof(uintptr_t) * 8) - 1);
    if ((raw & tag) != 0)
        return (uint32_t)((raw & ~tag) - 1u);
    return (uint32_t)raw;
}

static bool ImGui_ImplLuminary_UploadTextureRGBA(LRHIDevice device, LRHITexture texture, const void* pixels, uint32_t width, uint32_t height, uint32_t bytes_per_row, uint32_t total_size)
{
    LRHIError err = {};
    LRHIRegion region = {};
    region.x = 0;
    region.y = 0;
    region.z = 0;
    region.width = width;
    region.height = height;
    region.depth = 1;

    const LRHIDeviceInfo dev_info = lrhi_get_device_info(device);
    if (dev_info.backend == LUMINARY_RHI_BACKEND_METAL3 || dev_info.backend == LUMINARY_RHI_BACKEND_METAL4)
    {
        lrhi_texture_replace_region(texture, &region, 0, 0, const_cast<void*>(pixels), total_size, bytes_per_row, total_size, &err);
        ImGui_ImplLuminary_PrintError("texture_replace_region", &err);
        return err.severity != LUMINARY_RHI_ERROR_SEVERITY_ERROR;
    }

    LRHIBuffer staging_buffer = nullptr;
    LRHICommandQueue upload_queue = nullptr;
    LRHICommandList upload_cmd = nullptr;
    LRHIFence upload_fence = nullptr;
    LRHIResidencySet upload_rs = nullptr;
    bool success = false;

    LRHIBufferInfo staging_info = {};
    staging_info.size = total_size;
    staging_info.stride = 0;
    staging_info.usage = LUMINARY_RHI_BUFFER_USAGE_STAGING;
    staging_info.name = "ImGui Texture Staging Buffer";

    lrhi_create_buffer(device, &staging_info, &staging_buffer, &err);
    ImGui_ImplLuminary_PrintError("create_staging_buffer", &err);
    if (!staging_buffer)
        goto cleanup;

    {
        void* mapped = lrhi_buffer_map(staging_buffer, &err);
        ImGui_ImplLuminary_PrintError("map_staging_buffer", &err);
        if (!mapped)
            goto cleanup;
        memcpy(mapped, pixels, total_size);
        lrhi_buffer_unmap(staging_buffer);
    }

    lrhi_create_command_queue(device, &upload_queue, &err);
    ImGui_ImplLuminary_PrintError("create_upload_queue", &err);
    if (!upload_queue)
        goto cleanup;

    lrhi_create_command_list(upload_queue, &upload_cmd, &err);
    ImGui_ImplLuminary_PrintError("create_upload_command_list", &err);
    if (!upload_cmd)
        goto cleanup;

    lrhi_create_fence(device, 0, &upload_fence, &err);
    ImGui_ImplLuminary_PrintError("create_upload_fence", &err);
    if (!upload_fence)
        goto cleanup;

    lrhi_create_residency_set(device, &upload_rs, &err);
    ImGui_ImplLuminary_PrintError("create_upload_residency_set", &err);
    if (!upload_rs)
        goto cleanup;

    lrhi_residency_set_add_texture(upload_rs, texture, &err);
    ImGui_ImplLuminary_PrintError("residency_add_texture", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    lrhi_residency_set_add_buffer(upload_rs, staging_buffer, &err);
    ImGui_ImplLuminary_PrintError("residency_add_buffer", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    lrhi_residency_set_update(upload_rs, &err);
    ImGui_ImplLuminary_PrintError("residency_update", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    lrhi_command_queue_add_residency_set(upload_queue, upload_rs, &err);
    ImGui_ImplLuminary_PrintError("queue_add_residency_set", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    lrhi_command_list_begin(upload_cmd, &err);
    ImGui_ImplLuminary_PrintError("upload_cmd_begin", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    {
        LRHICopyPass copy_pass = lrhi_copy_pass_begin(upload_cmd, &err);
        ImGui_ImplLuminary_PrintError("copy_pass_begin", &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            goto cleanup;

        lrhi_copy_pass_copy_buffer_to_texture(copy_pass, staging_buffer, 0, bytes_per_row, total_size, texture, region, 0, 0, &err);
        ImGui_ImplLuminary_PrintError("copy_buffer_to_texture", &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        {
            lrhi_copy_pass_end(copy_pass, nullptr);
            goto cleanup;
        }

        lrhi_copy_pass_end(copy_pass, &err);
        ImGui_ImplLuminary_PrintError("copy_pass_end", &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            goto cleanup;
    }

    lrhi_command_list_end(upload_cmd, &err);
    ImGui_ImplLuminary_PrintError("upload_cmd_end", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    lrhi_command_queue_submit(upload_queue, &upload_cmd, 1, upload_fence, 1, nullptr, 0, &err);
    ImGui_ImplLuminary_PrintError("upload_queue_submit", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    lrhi_fence_wait(upload_fence, 1, 5000000000ULL, &err);
    ImGui_ImplLuminary_PrintError("upload_fence_wait", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        goto cleanup;

    success = true;

cleanup:
    if (upload_rs)
        lrhi_destroy_residency_set(upload_rs);
    if (upload_fence)
        lrhi_destroy_fence(upload_fence);
    if (upload_cmd)
        lrhi_destroy_command_list(upload_cmd);
    if (upload_queue)
        lrhi_destroy_command_queue(upload_queue);
    if (staging_buffer)
        lrhi_destroy_buffer(staging_buffer);
    return success;
}

static LRHIShaderModule ImGui_ImplLuminary_CreateShaderModule(LRHIDevice device, LuminaryShaderStage stage, LRHIShaderStage lrhi_stage, const char* entry_point)
{
    LuminaryShaderCompilerOptions opts = {};
    opts.shading_language = LUMINARY_SHADING_LANGUAGE_HLSL;
#if defined(LRHI_MACOS)
    opts.bytecode = LUMINARY_SHADING_BYTECODE_METALLIB;
#elif defined(LRHI_WINDOWS)
    opts.bytecode = LUMINARY_SHADING_BYTECODE_DXIL;
#else
    opts.bytecode = LUMINARY_SHADING_BYTECODE_SPIRV;
#endif
    opts.shader_stage = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code = const_cast<char*>(ImGui_ImplLuminary_ShaderSource);
    opts.source_code_size = strlen(ImGui_ImplLuminary_ShaderSource);
    opts.add_debug_symbols = 1;

    uint64_t bytecode_size = 0;
    uint8_t* bytecode = luminary_compile_shader(&opts, &bytecode_size);
    if (!bytecode || bytecode_size == 0)
        return nullptr;

    LRHIShaderModuleInfo module_info = {};
    module_info.stage = lrhi_stage;
    module_info.entry_point = entry_point;
    module_info.code = reinterpret_cast<const uint32_t*>(bytecode);
    module_info.code_size = (uint32_t)bytecode_size;
    module_info.name = "imgui_impl_luminary_shader";

    LRHIError err = {};
    LRHIShaderModule module = nullptr;
    lrhi_create_shader_module(device, &module_info, &module, &err);
    free(bytecode);

    ImGui_ImplLuminary_PrintError("create_shader_module", &err);
    return module;
}

static void ImGui_ImplLuminary_DestroyFrameRenderBuffers(ImGui_ImplLuminary_FrameRenderBuffers* buffers)
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();

    if (buffers->vertex_buffer_view)
    {
        lrhi_destroy_buffer_view(buffers->vertex_buffer_view);
        buffers->vertex_buffer_view = nullptr;
    }
    if (buffers->vertex_buffer)
    {
        ImGui_ImplLuminary_ResidencySetRemoveBufferAndUpdate(bd, buffers->vertex_buffer);
        lrhi_destroy_buffer(buffers->vertex_buffer);
        buffers->vertex_buffer = nullptr;
    }
    if (buffers->index_buffer)
    {
        ImGui_ImplLuminary_ResidencySetRemoveBufferAndUpdate(bd, buffers->index_buffer);
        lrhi_destroy_buffer(buffers->index_buffer);
        buffers->index_buffer = nullptr;
    }
    buffers->vertex_buffer_size = 0;
    buffers->index_buffer_size = 0;
}

static void ImGui_ImplLuminary_DestroyTexture(ImTextureData* tex)
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();
    ImGui_ImplLuminary_Texture* backend_tex = (ImGui_ImplLuminary_Texture*)tex->BackendUserData;
    if (!backend_tex)
        return;

    if (backend_tex->view)
        lrhi_destroy_texture_view(backend_tex->view);
    if (backend_tex->texture)
    {
        ImGui_ImplLuminary_ResidencySetRemoveTextureAndUpdate(bd, backend_tex->texture);
        lrhi_destroy_texture(backend_tex->texture);
    }

    IM_DELETE(backend_tex);
    tex->BackendUserData = nullptr;
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void ImGui_ImplLuminary_UpdateTexture(ImTextureData* tex)
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized!");

    if (tex->Status == ImTextureStatus_WantDestroy)
    {
        if (tex->UnusedFrames >= bd->init_info.num_frames_in_flight)
            ImGui_ImplLuminary_DestroyTexture(tex);
        return;
    }

    if (tex->Status == ImTextureStatus_WantCreate)
    {
        IM_ASSERT(tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32 || tex->Format == ImTextureFormat_Alpha8);

        LRHITextureInfo texture_info = {};
        texture_info.width = (uint32_t)tex->Width;
        texture_info.height = (uint32_t)tex->Height;
        texture_info.depth = 1;
        texture_info.mip_levels = 1;
        texture_info.array_layers = 1;
        texture_info.format = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
        texture_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        texture_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        texture_info.name = "ImGui Texture";

        LRHIError err = {};
        LRHITexture texture = nullptr;
        lrhi_create_texture(bd->init_info.device, &texture_info, &texture, &err);
        ImGui_ImplLuminary_PrintError("create_texture", &err);
        if (!texture)
            return;

        if (!ImGui_ImplLuminary_ResidencySetAddTextureAndUpdate(bd, texture))
        {
            lrhi_destroy_texture(texture);
            return;
        }

        LRHITextureViewInfo view_info = {};
        view_info.texture = texture;
        view_info.base_mip_level = 0;
        view_info.mip_level_count = 1;
        view_info.base_array_layer = 0;
        view_info.array_layer_count = 1;
        view_info.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
        view_info.usage = LUMINARY_RHI_TEXTURE_USAGE_SAMPLED;
        view_info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;

        LRHITextureView view = nullptr;
        err = {};
        lrhi_create_texture_view(bd->init_info.device, &view_info, &view, &err);
        ImGui_ImplLuminary_PrintError("create_texture_view", &err);
        if (!view)
        {
            ImGui_ImplLuminary_ResidencySetRemoveTextureAndUpdate(bd, texture);
            lrhi_destroy_texture(texture);
            return;
        }

        uint32_t bindless_index = lrhi_texture_view_get_bindless_index(view, &err);
        ImGui_ImplLuminary_PrintError("texture_view_get_bindless_index", &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        {
            lrhi_destroy_texture_view(view);
            ImGui_ImplLuminary_ResidencySetRemoveTextureAndUpdate(bd, texture);
            lrhi_destroy_texture(texture);
            return;
        }

        ImGui_ImplLuminary_Texture* backend_tex = IM_NEW(ImGui_ImplLuminary_Texture)();
        backend_tex->texture = texture;
        backend_tex->view = view;

        tex->BackendUserData = backend_tex;
        tex->SetTexID(ImGui_ImplLuminary_IndexToTexID(bindless_index));
        tex->SetStatus(ImTextureStatus_WantUpdates);
    }

    if (tex->Status == ImTextureStatus_WantUpdates)
    {
        ImGui_ImplLuminary_Texture* backend_tex = (ImGui_ImplLuminary_Texture*)tex->BackendUserData;
        if (!backend_tex)
            return;

        if (tex->Format == ImTextureFormat_RGBA32)
        {
            if (!ImGui_ImplLuminary_UploadTextureRGBA(bd->init_info.device,
                                                      backend_tex->texture,
                                                      tex->GetPixels(),
                                                      (uint32_t)tex->Width,
                                                      (uint32_t)tex->Height,
                                                      (uint32_t)tex->GetPitch(),
                                                      (uint32_t)tex->GetSizeInBytes()))
                return;
        }
        else
        {
            std::vector<uint8_t> rgba((size_t)tex->Width * (size_t)tex->Height * 4);
            const uint8_t* alpha = (const uint8_t*)tex->GetPixels();
            for (int i = 0; i < tex->Width * tex->Height; ++i)
            {
                rgba[(size_t)i * 4 + 0] = 255;
                rgba[(size_t)i * 4 + 1] = 255;
                rgba[(size_t)i * 4 + 2] = 255;
                rgba[(size_t)i * 4 + 3] = alpha[i];
            }

            if (!ImGui_ImplLuminary_UploadTextureRGBA(bd->init_info.device,
                                                      backend_tex->texture,
                                                      rgba.data(),
                                                      (uint32_t)tex->Width,
                                                      (uint32_t)tex->Height,
                                                      (uint32_t)tex->Width * 4,
                                                      (uint32_t)rgba.size()))
                return;
        }

        tex->SetStatus(ImTextureStatus_OK);
    }
}

bool ImGui_ImplLuminary_CreateDeviceObjects()
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();
    if (!bd)
        return false;

    if (bd->pipeline)
        return true;

    bd->vertex_shader = ImGui_ImplLuminary_CreateShaderModule(bd->init_info.device, LUMINARY_SHADER_STAGE_VERTEX, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain");
    bd->fragment_shader = ImGui_ImplLuminary_CreateShaderModule(bd->init_info.device, LUMINARY_SHADER_STAGE_FRAGMENT, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!bd->vertex_shader || !bd->fragment_shader)
        return false;

    LRHISamplerInfo sampler_info = {};
    sampler_info.min_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    sampler_info.mag_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    sampler_info.mipmap_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    sampler_info.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.min_lod = 0.0f;
    sampler_info.max_lod = 0.0f;
    sampler_info.name = "ImGui Sampler";

    LRHIError err = {};
    lrhi_create_sampler(bd->init_info.device, &sampler_info, &bd->sampler, &err);
    ImGui_ImplLuminary_PrintError("create_sampler", &err);
    if (!bd->sampler)
        return false;

    bd->sampler_bindless_index = lrhi_sampler_get_bindless_index(bd->sampler, &err);
    ImGui_ImplLuminary_PrintError("sampler_get_bindless_index", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        return false;

    LRHIRenderPipelineInfo pipeline_info = {};
    pipeline_info.fill_mode = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    pipeline_info.cull_mode = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
    pipeline_info.front_face = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_info.topology = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
    pipeline_info.depth_test_enable = 0;
    pipeline_info.depth_write_enable = 0;
    pipeline_info.depth_stencil_format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    pipeline_info.blend_enable[0] = 1;
    pipeline_info.blend_src_rgb_factor[0] = LUMINARY_RHI_BLEND_FACTOR_SRC_ALPHA;
    pipeline_info.blend_dst_rgb_factor[0] = LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline_info.blend_rgb_op[0] = LUMINARY_RHI_BLEND_OPERATION_ADD;
    pipeline_info.blend_src_alpha_factor[0] = LUMINARY_RHI_BLEND_FACTOR_ONE;
    pipeline_info.blend_dst_alpha_factor[0] = LUMINARY_RHI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline_info.blend_alpha_op[0] = LUMINARY_RHI_BLEND_OPERATION_ADD;
    pipeline_info.render_target_formats[0] = bd->init_info.render_target_format;
    pipeline_info.render_target_count = 1;
    pipeline_info.vertex_shader = bd->vertex_shader;
    pipeline_info.fragment_shader = bd->fragment_shader;
    pipeline_info.name = "ImGui Pipeline";

    err = {};
    lrhi_create_render_pipeline(bd->init_info.device, &pipeline_info, &bd->pipeline, &err);
    ImGui_ImplLuminary_PrintError("create_render_pipeline", &err);
    return bd->pipeline != nullptr;
}

void ImGui_ImplLuminary_InvalidateDeviceObjects()
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();
    if (!bd)
        return;

    if (bd->pipeline)
    {
        lrhi_destroy_render_pipeline(bd->pipeline);
        bd->pipeline = nullptr;
    }
    if (bd->vertex_shader)
    {
        lrhi_destroy_shader_module(bd->vertex_shader);
        bd->vertex_shader = nullptr;
    }
    if (bd->fragment_shader)
    {
        lrhi_destroy_shader_module(bd->fragment_shader);
        bd->fragment_shader = nullptr;
    }
    if (bd->sampler)
    {
        lrhi_destroy_sampler(bd->sampler);
        bd->sampler = nullptr;
    }
}

bool ImGui_ImplLuminary_Init(ImGui_ImplLuminary_InitInfo* info)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    IM_ASSERT(info != nullptr);
    IM_ASSERT(info->device != nullptr);
    IM_ASSERT(info->num_frames_in_flight > 0);

    ImGui_ImplLuminary_Data* bd = IM_NEW(ImGui_ImplLuminary_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_luminary";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    bd->init_info = *info;
    bd->frame_render_buffers = new ImGui_ImplLuminary_FrameRenderBuffers[info->num_frames_in_flight];

    return true;
}

void ImGui_ImplLuminary_Shutdown()
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
    {
        if (tex->RefCount == 1 && tex->BackendUserData != nullptr)
            ImGui_ImplLuminary_DestroyTexture(tex);
    }

    for (int n = 0; n < bd->init_info.num_frames_in_flight; n++)
        ImGui_ImplLuminary_DestroyFrameRenderBuffers(&bd->frame_render_buffers[n]);

    delete[] bd->frame_render_buffers;
    ImGui_ImplLuminary_InvalidateDeviceObjects();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);

    IM_DELETE(bd);
}

void ImGui_ImplLuminary_NewFrame()
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplLuminary_Init()?");

    if (!bd->pipeline)
        ImGui_ImplLuminary_CreateDeviceObjects();
}

void ImGui_ImplLuminary_RenderDrawData(ImDrawData* draw_data, LRHIRenderPass render_pass)
{
    ImGui_ImplLuminary_Data* bd = ImGui_ImplLuminary_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplLuminary_Init()?");
    if (!draw_data || draw_data->TotalVtxCount == 0 || draw_data->TotalIdxCount == 0)
        return;

    if (draw_data->Textures != nullptr)
        for (ImTextureData* tex : *draw_data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplLuminary_UpdateTexture(tex);

    bd->frame_index = (bd->frame_index + 1) % bd->init_info.num_frames_in_flight;
    ImGui_ImplLuminary_FrameRenderBuffers* frb = &bd->frame_render_buffers[bd->frame_index];

    if (frb->vertex_buffer == nullptr || frb->vertex_buffer_size < draw_data->TotalVtxCount)
    {
        if (frb->vertex_buffer_view)
        {
            lrhi_destroy_buffer_view(frb->vertex_buffer_view);
            frb->vertex_buffer_view = nullptr;
        }
        if (frb->vertex_buffer)
        {
            lrhi_destroy_buffer(frb->vertex_buffer);
            frb->vertex_buffer = nullptr;
        }

        frb->vertex_buffer_size = draw_data->TotalVtxCount + 5000;

        LRHIBufferInfo vb_info = {};
        vb_info.size = (uint64_t)frb->vertex_buffer_size * sizeof(ImDrawVert);
        vb_info.stride = sizeof(ImDrawVert);
        vb_info.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ | LUMINARY_RHI_BUFFER_USAGE_STAGING);
        vb_info.name = "ImGui Vertex Buffer";

        LRHIError err = {};
        lrhi_create_buffer(bd->init_info.device, &vb_info, &frb->vertex_buffer, &err);
        ImGui_ImplLuminary_PrintError("create_vertex_buffer", &err);
        if (!frb->vertex_buffer)
            return;

        if (!ImGui_ImplLuminary_ResidencySetAddBufferAndUpdate(bd, frb->vertex_buffer))
        {
            lrhi_destroy_buffer(frb->vertex_buffer);
            frb->vertex_buffer = nullptr;
            return;
        }

        LRHIBufferViewInfo vbv_info = {};
        vbv_info.buffer = frb->vertex_buffer;
        vbv_info.offset = 0;
        vbv_info.view_type = LUMINARY_RHI_BUFFER_VIEW_TYPE_STRUCTURED;

        err = {};
        lrhi_create_buffer_view(bd->init_info.device, &vbv_info, &frb->vertex_buffer_view, &err);
        ImGui_ImplLuminary_PrintError("create_vertex_buffer_view", &err);
        if (!frb->vertex_buffer_view)
        {
            ImGui_ImplLuminary_ResidencySetRemoveBufferAndUpdate(bd, frb->vertex_buffer);
            lrhi_destroy_buffer(frb->vertex_buffer);
            frb->vertex_buffer = nullptr;
            return;
        }
    }

    if (frb->index_buffer == nullptr || frb->index_buffer_size < draw_data->TotalIdxCount)
    {
        if (frb->index_buffer)
        {
            lrhi_destroy_buffer(frb->index_buffer);
            frb->index_buffer = nullptr;
        }

        frb->index_buffer_size = draw_data->TotalIdxCount + 10000;

        LRHIBufferInfo ib_info = {};
        ib_info.size = (uint64_t)frb->index_buffer_size * sizeof(ImDrawIdx);
        ib_info.stride = sizeof(ImDrawIdx);
        ib_info.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_INDEX | LUMINARY_RHI_BUFFER_USAGE_STAGING);
        ib_info.name = "ImGui Index Buffer";

        LRHIError err = {};
        lrhi_create_buffer(bd->init_info.device, &ib_info, &frb->index_buffer, &err);
        ImGui_ImplLuminary_PrintError("create_index_buffer", &err);
        if (!frb->index_buffer)
            return;

        if (!ImGui_ImplLuminary_ResidencySetAddBufferAndUpdate(bd, frb->index_buffer))
        {
            lrhi_destroy_buffer(frb->index_buffer);
            frb->index_buffer = nullptr;
            return;
        }
    }

    LRHIError err = {};
    ImDrawVert* vtx_dst = (ImDrawVert*)lrhi_buffer_map(frb->vertex_buffer, &err);
    ImGui_ImplLuminary_PrintError("map_vertex_buffer", &err);
    if (!vtx_dst)
        return;

    ImDrawIdx* idx_dst = (ImDrawIdx*)lrhi_buffer_map(frb->index_buffer, &err);
    ImGui_ImplLuminary_PrintError("map_index_buffer", &err);
    if (!idx_dst)
    {
        lrhi_buffer_unmap(frb->vertex_buffer);
        return;
    }

    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* draw_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, draw_list->VtxBuffer.Data, (size_t)draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, draw_list->IdxBuffer.Data, (size_t)draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += draw_list->VtxBuffer.Size;
        idx_dst += draw_list->IdxBuffer.Size;
    }

    lrhi_buffer_unmap(frb->vertex_buffer);
    lrhi_buffer_unmap(frb->index_buffer);

    const float L = draw_data->DisplayPos.x;
    const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float T = draw_data->DisplayPos.y;
    const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

    ImGui_ImplLuminary_PushConstants pc = {};
    pc.projection[0][0] = 2.0f / (R - L);
    pc.projection[1][1] = 2.0f / (T - B);
    pc.projection[2][2] = 0.5f;
    pc.projection[3][0] = (R + L) / (L - R);
    pc.projection[3][1] = (T + B) / (B - T);
    pc.projection[3][2] = 0.5f;
    pc.projection[3][3] = 1.0f;

    err = {};
    pc.vertex_buffer_index = lrhi_buffer_view_get_bindless_index(frb->vertex_buffer_view, &err);
    ImGui_ImplLuminary_PrintError("buffer_view_get_bindless_index", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        return;
    pc.sampler_index = bd->sampler_bindless_index;

    err = {};
    lrhi_render_pass_set_render_pipeline(render_pass, bd->pipeline, &err);
    ImGui_ImplLuminary_PrintError("set_render_pipeline", &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        return;

    const int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;

    err = {};
    lrhi_render_pass_set_viewport(render_pass, 0, 0, (uint32_t)fb_width, (uint32_t)fb_height, 0.0f, 1.0f, &err);
    ImGui_ImplLuminary_PrintError("set_viewport", &err);

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplLuminary_RenderState render_state = { render_pass, bd->pipeline };
    platform_io.Renderer_RenderState = &render_state;

    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* draw_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                {
                    err = {};
                    lrhi_render_pass_set_render_pipeline(render_pass, bd->pipeline, &err);
                    lrhi_render_pass_set_viewport(render_pass, 0, 0, (uint32_t)fb_width, (uint32_t)fb_height, 0.0f, 1.0f, &err);
                }
                else
                {
                    pcmd->UserCallback(draw_list, pcmd);
                }
            }
            else
            {
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                if (clip_min.x < 0.0f) clip_min.x = 0.0f;
                if (clip_min.y < 0.0f) clip_min.y = 0.0f;
                if (clip_max.x > fb_width) clip_max.x = (float)fb_width;
                if (clip_max.y > fb_height) clip_max.y = (float)fb_height;
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                const uint32_t scissor_x = (uint32_t)clip_min.x;
                const uint32_t scissor_y = (uint32_t)clip_min.y;
                const uint32_t scissor_w = (uint32_t)(clip_max.x - clip_min.x);
                const uint32_t scissor_h = (uint32_t)(clip_max.y - clip_min.y);

                pc.texture_index = ImGui_ImplLuminary_TexIDToIndex(pcmd->GetTexID());
                pc.vertex_offset = (uint32_t)(pcmd->VtxOffset + global_vtx_offset);

                err = {};
                lrhi_render_pass_set_scissor(render_pass, scissor_x, scissor_y, scissor_w, scissor_h, &err);
                ImGui_ImplLuminary_PrintError("set_scissor", &err);

                err = {};
                lrhi_render_pass_set_push_constants(render_pass, &pc, sizeof(pc), &err);
                ImGui_ImplLuminary_PrintError("set_push_constants", &err);

                err = {};
                lrhi_render_pass_draw_indexed(render_pass,
                                              pcmd->ElemCount,
                                              1,
                                              pcmd->IdxOffset + global_idx_offset,
                                              0,
                                              0,
                                              frb->index_buffer,
                                              sizeof(ImDrawIdx),
                                              &err);
                ImGui_ImplLuminary_PrintError("draw_indexed", &err);
            }
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
    }

    platform_io.Renderer_RenderState = nullptr;
}

#endif // #ifndef IMGUI_DISABLE