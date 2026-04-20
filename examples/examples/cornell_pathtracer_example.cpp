#include "cornell_pathtracer_example.h"

#include "extras/shader_compiler/luminary_shader_compiler.h"
#include "../ext/hmm/HMM.h"
#include "../ext/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

static std::string read_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string src(sz, '\0');
    fread(src.data(), 1, sz, f);
    fclose(f);
    return src;
}

static std::pair<uint8_t*, uint64_t> compile_stage(const std::string& source,
                                                   LuminaryShaderStage stage,
                                                   const char* entry_point,
                                                   bool use_raytracing,
                                                   bool use_point_topology)
{
    LuminaryShaderCompilerOptions opts = {};
    opts.shading_language = LUMINARY_SHADING_LANGUAGE_HLSL;
    opts.bytecode = LUMINARY_SHADING_BYTECODE_METALLIB;
    opts.shader_stage = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code = const_cast<char*>(source.data());
    opts.source_code_size = source.size();
    opts.add_debug_symbols = 1;
    opts.use_raytracing = use_raytracing ? 1 : 0;
    opts.use_point_topology = use_point_topology ? 1 : 0;

    uint64_t size = 0;
    uint8_t* bytecode = luminary_compile_shader(&opts, &size);
    return {bytecode, size};
}

static LRHIShaderModule make_module(LRHIDevice device,
                                    uint8_t* bytecode,
                                    uint64_t size,
                                    LRHIShaderStage stage,
                                    const char* entry_point)
{
    LRHIShaderModuleInfo info = {};
    info.stage = stage;
    info.entry_point = entry_point;
    info.code = reinterpret_cast<const uint32_t*>(bytecode);
    info.code_size = static_cast<uint32_t>(size);

    LRHIShaderModule module = nullptr;
    LRHIError err = {};
    lrhi_create_shader_module(device, &info, &module, &err);
    free(bytecode);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_shader_module: %s\n", err.message);
        return nullptr;
    }

    return module;
}

static LRHITextureView make_view(LRHIDevice device,
                                 LRHITexture texture,
                                 LRHITextureUsage usage)
{
    LRHITextureViewInfo info = {};
    info.texture = texture;
    info.base_mip_level = 0;
    info.mip_level_count = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
    info.base_array_layer = 0;
    info.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
    info.format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    info.usage = usage;
    info.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;

    LRHITextureView view = nullptr;
    LRHIError err = {};
    lrhi_create_texture_view(device, &info, &view, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_texture_view: %s\n", err.message);
        return nullptr;
    }

    return view;
}

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

} // namespace

CornellPathtracerExample::CornellPathtracerExample(LRHIDevice in_device, LRHITextureFormat in_render_target_format)
    : device(in_device)
    , render_target_format(in_render_target_format)
    , trace_compute_shader(nullptr)
    , tonemap_vertex_shader(nullptr)
    , tonemap_fragment_shader(nullptr)
    , trace_pipeline(nullptr)
    , tonemap_pipeline(nullptr)
    , vertex_buffer(nullptr)
    , index_buffer(nullptr)
    , scratch_buffer(nullptr)
    , blas(nullptr)
    , tlas(nullptr)
    , linear_sampler(nullptr)
    , sampler_handle(0)
    , tlas_handle(0)
    , vertex_count(0)
    , index_count(0)
    , blas_scratch_size_aligned(0)
    , trace_width(0)
    , trace_height(0)
    , active_history_index(0)
    , static_resources_added_to_residency(false)
    , history_resources_added_to_residency(false)
    , acceleration_structures_built(false)
    , accumulation_samples(0)
    , force_reset_accumulation(true)
    , half_resolution(false)
    , max_bounces(3)
    , samples_per_frame(1)
    , exposure(1.3f)
    , tonemap_mode(0)
    , camera_yaw(0.0f)
    , camera_pitch(0.05f)
    , camera_distance(2.8f)
    , show_help(true)
{
    for (int i = 0; i < 2; ++i) {
        accum_textures[i] = nullptr;
        prev_textures[i] = nullptr;
        accum_storage_views[i] = nullptr;
        accum_sampled_views[i] = nullptr;
        prev_storage_views[i] = nullptr;
        prev_sampled_views[i] = nullptr;
        accum_storage_handles[i] = 0;
        accum_sampled_handles[i] = 0;
        prev_storage_handles[i] = 0;
        prev_sampled_handles[i] = 0;
    }

    if (!create_pipelines()) {
        return;
    }
    if (!create_scene_geometry()) {
        return;
    }
    if (!create_raytracing_structures()) {
        return;
    }

    LRHIError err = {};
    LRHISamplerInfo si = {};
    si.min_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    si.mag_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    si.mipmap_filter = LUMINARY_RHI_SAMPLER_FILTER_LINEAR;
    si.address_mode_u = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.address_mode_v = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.address_mode_w = LUMINARY_RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.min_lod = 0.0f;
    si.max_lod = 0.0f;
    si.name = "Cornell Linear Sampler";
    lrhi_create_sampler(device, &si, &linear_sampler, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !linear_sampler) {
        printf("create_sampler: %s\n", err.message);
        return;
    }

    sampler_handle = lrhi_sampler_get_bindless_index(linear_sampler, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("sampler_get_bindless_index: %s\n", err.message);
        return;
    }
}

CornellPathtracerExample::~CornellPathtracerExample()
{
    destroy_history_targets();

    if (linear_sampler) {
        lrhi_destroy_sampler(linear_sampler);
        linear_sampler = nullptr;
    }

    if (tlas) {
        lrhi_destroy_top_level_acceleration_structure(tlas);
        tlas = nullptr;
    }
    if (blas) {
        lrhi_destroy_bottom_level_acceleration_structure(blas);
        blas = nullptr;
    }

    if (scratch_buffer) {
        lrhi_destroy_buffer(scratch_buffer);
        scratch_buffer = nullptr;
    }
    if (index_buffer) {
        lrhi_destroy_buffer(index_buffer);
        index_buffer = nullptr;
    }
    if (vertex_buffer) {
        lrhi_destroy_buffer(vertex_buffer);
        vertex_buffer = nullptr;
    }

    if (tonemap_pipeline) {
        lrhi_destroy_render_pipeline(tonemap_pipeline);
        tonemap_pipeline = nullptr;
    }
    if (trace_pipeline) {
        lrhi_destroy_compute_pipeline(trace_pipeline);
        trace_pipeline = nullptr;
    }

    if (tonemap_fragment_shader) {
        lrhi_destroy_shader_module(tonemap_fragment_shader);
        tonemap_fragment_shader = nullptr;
    }
    if (tonemap_vertex_shader) {
        lrhi_destroy_shader_module(tonemap_vertex_shader);
        tonemap_vertex_shader = nullptr;
    }
    if (trace_compute_shader) {
        lrhi_destroy_shader_module(trace_compute_shader);
        trace_compute_shader = nullptr;
    }
}

const char* CornellPathtracerExample::name() const
{
    return "Cornell Pathtracer";
}

bool CornellPathtracerExample::is_ready() const
{
    return trace_pipeline != nullptr &&
           tonemap_pipeline != nullptr &&
           vertex_buffer != nullptr &&
           index_buffer != nullptr &&
           scratch_buffer != nullptr &&
           blas != nullptr &&
           tlas != nullptr &&
           linear_sampler != nullptr;
}

bool CornellPathtracerExample::create_pipelines()
{
    LRHIError err = {};

    std::string trace_src = read_file("shaders/examples/cornell_pathtracer_trace_compute.hlsl");
    std::string tonemap_src = read_file("shaders/examples/cornell_pathtracer_tonemap.hlsl");
    if (trace_src.empty() || tonemap_src.empty()) {
        printf("Failed to read Cornell pathtracer shaders\n");
        return false;
    }

    auto [cs_bc, cs_sz] = compile_stage(trace_src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain", true, false);
    auto [vs_bc, vs_sz] = compile_stage(tonemap_src, LUMINARY_SHADER_STAGE_VERTEX, "VSMain", false, false);
    auto [ps_bc, ps_sz] = compile_stage(tonemap_src, LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain", false, false);
    if (!cs_bc || !vs_bc || !ps_bc) {
        free(cs_bc);
        free(vs_bc);
        free(ps_bc);
        printf("Cornell shader compilation failed\n");
        return false;
    }

    trace_compute_shader = make_module(device, cs_bc, cs_sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain");
    tonemap_vertex_shader = make_module(device, vs_bc, vs_sz, LUMINARY_RHI_SHADER_STAGE_VERTEX, "VSMain");
    tonemap_fragment_shader = make_module(device, ps_bc, ps_sz, LUMINARY_RHI_SHADER_STAGE_FRAGMENT, "PSMain");
    if (!trace_compute_shader || !tonemap_vertex_shader || !tonemap_fragment_shader) {
        return false;
    }

    LRHIComputePipelineInfo cpi = {};
    cpi.compute_shader = trace_compute_shader;
    cpi.supports_indirect_commands = 0;
    cpi.name = "Cornell Trace Pipeline";
    lrhi_create_compute_pipeline(device, &cpi, &trace_pipeline, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !trace_pipeline) {
        printf("create_compute_pipeline: %s\n", err.message);
        return false;
    }

    LRHIRenderPipelineInfo rpi = {};
    rpi.fill_mode = LUMINARY_RHI_PIPELINE_FILL_MODE_SOLID;
    rpi.cull_mode = LUMINARY_RHI_PIPELINE_CULL_MODE_NONE;
    rpi.front_face = LUMINARY_RHI_PIPELINE_FRONT_FACE_COUNTER_CLOCKWISE;
    rpi.topology = LUMINARY_RHI_PIPELINE_TOPOLOGY_TRIANGLE_LIST;
    rpi.depth_test_enable = 0;
    rpi.depth_write_enable = 0;
    rpi.depth_stencil_format = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    rpi.render_target_formats[0] = render_target_format;
    rpi.render_target_count = 1;
    rpi.vertex_shader = tonemap_vertex_shader;
    rpi.fragment_shader = tonemap_fragment_shader;
    rpi.name = "Cornell Tonemap Pipeline";
    lrhi_create_render_pipeline(device, &rpi, &tonemap_pipeline, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !tonemap_pipeline) {
        printf("create_render_pipeline: %s\n", err.message);
        return false;
    }

    return true;
}

bool CornellPathtracerExample::create_scene_geometry()
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto add_quad = [&](HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, HMM_Vec3 d) {
        uint32_t base = (uint32_t)vertices.size();
        vertices.push_back({a.X, a.Y, a.Z});
        vertices.push_back({b.X, b.Y, b.Z});
        vertices.push_back({c.X, c.Y, c.Z});
        vertices.push_back({d.X, d.Y, d.Z});
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    };

    auto add_box = [&](HMM_Vec3 bmin, HMM_Vec3 bmax) {
        HMM_Vec3 p000 = HMM_V3(bmin.X, bmin.Y, bmin.Z);
        HMM_Vec3 p001 = HMM_V3(bmin.X, bmin.Y, bmax.Z);
        HMM_Vec3 p010 = HMM_V3(bmin.X, bmax.Y, bmin.Z);
        HMM_Vec3 p011 = HMM_V3(bmin.X, bmax.Y, bmax.Z);
        HMM_Vec3 p100 = HMM_V3(bmax.X, bmin.Y, bmin.Z);
        HMM_Vec3 p101 = HMM_V3(bmax.X, bmin.Y, bmax.Z);
        HMM_Vec3 p110 = HMM_V3(bmax.X, bmax.Y, bmin.Z);
        HMM_Vec3 p111 = HMM_V3(bmax.X, bmax.Y, bmax.Z);

        add_quad(p000, p001, p011, p010); // -X
        add_quad(p100, p110, p111, p101); // +X
        add_quad(p000, p100, p101, p001); // -Y
        add_quad(p010, p011, p111, p110); // +Y
        add_quad(p000, p010, p110, p100); // -Z
        add_quad(p001, p101, p111, p011); // +Z
    };

    // Cornell room (open front at z=-1)
    add_quad(HMM_V3(-1.0f, 0.0f, -1.0f), HMM_V3(1.0f, 0.0f, -1.0f), HMM_V3(1.0f, 0.0f, 1.0f), HMM_V3(-1.0f, 0.0f, 1.0f)); // floor
    add_quad(HMM_V3(-1.0f, 2.0f, -1.0f), HMM_V3(-1.0f, 2.0f, 1.0f), HMM_V3(1.0f, 2.0f, 1.0f), HMM_V3(1.0f, 2.0f, -1.0f));   // ceiling
    add_quad(HMM_V3(-1.0f, 0.0f, 1.0f), HMM_V3(1.0f, 0.0f, 1.0f), HMM_V3(1.0f, 2.0f, 1.0f), HMM_V3(-1.0f, 2.0f, 1.0f));     // back wall
    add_quad(HMM_V3(-1.0f, 0.0f, -1.0f), HMM_V3(-1.0f, 0.0f, 1.0f), HMM_V3(-1.0f, 2.0f, 1.0f), HMM_V3(-1.0f, 2.0f, -1.0f)); // left wall
    add_quad(HMM_V3(1.0f, 0.0f, -1.0f), HMM_V3(1.0f, 2.0f, -1.0f), HMM_V3(1.0f, 2.0f, 1.0f), HMM_V3(1.0f, 0.0f, 1.0f));      // right wall

    // Light quad near ceiling
    add_quad(HMM_V3(-0.25f, 1.99f, -0.25f), HMM_V3(0.25f, 1.99f, -0.25f), HMM_V3(0.25f, 1.99f, 0.25f), HMM_V3(-0.25f, 1.99f, 0.25f));

    // Inner boxes
    add_box(HMM_V3(-0.55f, 0.0f, -0.15f), HMM_V3(-0.05f, 0.6f, 0.4f));
    add_box(HMM_V3(0.2f, 0.0f, -0.7f), HMM_V3(0.7f, 1.2f, -0.2f));

    vertex_count = (uint32_t)vertices.size();
    index_count = (uint32_t)indices.size();

    LRHIError err = {};

    LRHIBufferInfo vbi = {};
    vbi.size = (uint64_t)vertices.size() * sizeof(Vertex);
    vbi.stride = sizeof(Vertex);
    vbi.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
    vbi.name = "Cornell Vertex Buffer";
    lrhi_create_buffer(device, &vbi, &vertex_buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !vertex_buffer) {
        printf("create_vertex_buffer: %s\n", err.message);
        return false;
    }

    void* vptr = lrhi_buffer_map(vertex_buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !vptr) {
        printf("map_vertex_buffer: %s\n", err.message);
        return false;
    }
    memcpy(vptr, vertices.data(), (size_t)vbi.size);
    lrhi_buffer_unmap(vertex_buffer);

    LRHIBufferInfo ibi = {};
    ibi.size = (uint64_t)indices.size() * sizeof(uint32_t);
    ibi.stride = sizeof(uint32_t);
    ibi.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
    ibi.name = "Cornell Index Buffer";
    lrhi_create_buffer(device, &ibi, &index_buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !index_buffer) {
        printf("create_index_buffer: %s\n", err.message);
        return false;
    }

    void* iptr = lrhi_buffer_map(index_buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !iptr) {
        printf("map_index_buffer: %s\n", err.message);
        return false;
    }
    memcpy(iptr, indices.data(), (size_t)ibi.size);
    lrhi_buffer_unmap(index_buffer);

    return true;
}

bool CornellPathtracerExample::create_raytracing_structures()
{
    LRHIError err = {};

    LRHIBLASGeometryInfo geom = {};
    geom.opaque = 1;
    geom.triangles.vertex_buffer = vertex_buffer;
    geom.triangles.vertex_offset = 0;
    geom.triangles.vertex_count = vertex_count;
    geom.triangles.index_buffer = index_buffer;
    geom.triangles.index_offset = 0;
    geom.triangles.index_count = index_count;

    LRHIBLASInfo bi = {};
    bi.allow_update = 0;
    bi.geometry_type = LUMINARY_RHI_BOTTOM_LEVEL_GEOMETRY_TYPE_TRIANGLES;
    bi.geometry_count = 1;
    bi.geometries = &geom;
    bi.name = "Cornell BLAS";
    lrhi_create_bottom_level_acceleration_structure(device, &bi, &blas, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !blas) {
        printf("create_blas: %s\n", err.message);
        return false;
    }

    LRHITLASInfo ti = {};
    ti.max_instance_count = 1;
    ti.name = "Cornell TLAS";
    lrhi_create_top_level_acceleration_structure(device, &ti, &tlas, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !tlas) {
        printf("create_tlas: %s\n", err.message);
        return false;
    }

    static const float kIdentity[12] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
    };

    LRHITLASInstanceInfo inst = {};
    inst.blas = blas;
    inst.user_id = 0;
    inst.opaque = 1;
    memcpy(inst.transform, kIdentity, sizeof(kIdentity));
    lrhi_add_top_level_acceleration_structure_instance(tlas, &inst, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("add_tlas_instance: %s\n", err.message);
        return false;
    }

    LRHIAccelerationStructureBufferSizes bs = lrhi_bottom_level_acceleration_structure_get_build_scratch_size(blas, &err);
    LRHIAccelerationStructureBufferSizes ts = lrhi_top_level_acceleration_structure_get_build_scratch_size(tlas, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("query_scratch_sizes: %s\n", err.message);
        return false;
    }

    blas_scratch_size_aligned = align_up(bs.build_scratch_size, 256u);
    uint64_t scratch_total = blas_scratch_size_aligned + ts.build_scratch_size;

    LRHIBufferInfo sbi = {};
    sbi.size = scratch_total;
    sbi.stride = 1;
    sbi.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ | LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
    sbi.name = "Cornell RT Scratch";
    lrhi_create_buffer(device, &sbi, &scratch_buffer, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !scratch_buffer) {
        printf("create_scratch_buffer: %s\n", err.message);
        return false;
    }

    uint64_t idx64 = lrhi_top_level_acceleration_structure_get_bindless_index(tlas, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("tlas_get_bindless_index: %s\n", err.message);
        return false;
    }
    tlas_handle = (uint32_t)idx64;

    return true;
}

bool CornellPathtracerExample::create_history_targets(uint32_t width,
                                                      uint32_t height,
                                                      LRHIResidencySet residency_set)
{
    if (width == 0 || height == 0) {
        return false;
    }

    uint32_t desired_w = half_resolution ? std::max(1u, width / 2u) : width;
    uint32_t desired_h = half_resolution ? std::max(1u, height / 2u) : height;

    if (desired_w == trace_width && desired_h == trace_height &&
        accum_textures[0] && accum_textures[1] && prev_textures[0] && prev_textures[1]) {
        return true;
    }

    if (history_resources_added_to_residency && residency_set) {
        LRHIError remove_err = {};
        for (int i = 0; i < 2; ++i) {
            if (accum_textures[i]) {
                lrhi_residency_set_remove_texture(residency_set, accum_textures[i], &remove_err);
            }
            if (prev_textures[i]) {
                lrhi_residency_set_remove_texture(residency_set, prev_textures[i], &remove_err);
            }
        }
    }

    destroy_history_targets();

    LRHIError err = {};
    for (int i = 0; i < 2; ++i) {
        LRHITextureInfo ti = {};
        ti.width = desired_w;
        ti.height = desired_h;
        ti.depth = 1;
        ti.mip_levels = 1;
        ti.array_layers = 1;
        ti.format = LUMINARY_RHI_TEXTURE_FORMAT_R16G16B16A16_FLOAT;
        ti.usage = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
        ti.name = (i == 0) ? "Cornell Accum A" : "Cornell Accum B";
        lrhi_create_texture(device, &ti, &accum_textures[i], &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !accum_textures[i]) {
            printf("create_accum_texture: %s\n", err.message);
            return false;
        }

        ti.name = (i == 0) ? "Cornell Prev A" : "Cornell Prev B";
        lrhi_create_texture(device, &ti, &prev_textures[i], &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !prev_textures[i]) {
            printf("create_prev_texture: %s\n", err.message);
            return false;
        }

        accum_storage_views[i] = make_view(device, accum_textures[i], LUMINARY_RHI_TEXTURE_USAGE_STORAGE);
        accum_sampled_views[i] = make_view(device, accum_textures[i], LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        prev_storage_views[i] = make_view(device, prev_textures[i], LUMINARY_RHI_TEXTURE_USAGE_STORAGE);
        prev_sampled_views[i] = make_view(device, prev_textures[i], LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
        if (!accum_storage_views[i] || !accum_sampled_views[i] || !prev_storage_views[i] || !prev_sampled_views[i]) {
            return false;
        }

        accum_storage_handles[i] = lrhi_texture_view_get_bindless_index(accum_storage_views[i], &err);
        accum_sampled_handles[i] = lrhi_texture_view_get_bindless_index(accum_sampled_views[i], &err);
        prev_storage_handles[i] = lrhi_texture_view_get_bindless_index(prev_storage_views[i], &err);
        prev_sampled_handles[i] = lrhi_texture_view_get_bindless_index(prev_sampled_views[i], &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("history texture bindless index: %s\n", err.message);
            return false;
        }
    }

    trace_width = desired_w;
    trace_height = desired_h;
    active_history_index = 0;
    accumulation_samples = 0;
    force_reset_accumulation = true;
    history_resources_added_to_residency = false;
    return true;
}

void CornellPathtracerExample::destroy_history_targets()
{
    for (int i = 0; i < 2; ++i) {
        if (prev_sampled_views[i]) {
            lrhi_destroy_texture_view(prev_sampled_views[i]);
            prev_sampled_views[i] = nullptr;
        }
        if (prev_storage_views[i]) {
            lrhi_destroy_texture_view(prev_storage_views[i]);
            prev_storage_views[i] = nullptr;
        }
        if (accum_sampled_views[i]) {
            lrhi_destroy_texture_view(accum_sampled_views[i]);
            accum_sampled_views[i] = nullptr;
        }
        if (accum_storage_views[i]) {
            lrhi_destroy_texture_view(accum_storage_views[i]);
            accum_storage_views[i] = nullptr;
        }

        if (prev_textures[i]) {
            lrhi_destroy_texture(prev_textures[i]);
            prev_textures[i] = nullptr;
        }
        if (accum_textures[i]) {
            lrhi_destroy_texture(accum_textures[i]);
            accum_textures[i] = nullptr;
        }

        accum_storage_handles[i] = 0;
        accum_sampled_handles[i] = 0;
        prev_storage_handles[i] = 0;
        prev_sampled_handles[i] = 0;
    }

    trace_width = 0;
    trace_height = 0;
    active_history_index = 0;
    history_resources_added_to_residency = false;
}

bool CornellPathtracerExample::ensure_residency(LRHIResidencySet residency_set)
{
    if (!residency_set) {
        return false;
    }

    LRHIError err = {};

    if (!static_resources_added_to_residency) {
        lrhi_residency_set_add_buffer(residency_set, vertex_buffer, &err);
        lrhi_residency_set_add_buffer(residency_set, index_buffer, &err);
        lrhi_residency_set_add_buffer(residency_set, scratch_buffer, &err);
        lrhi_residency_set_add_blas(residency_set, blas, &err);
        lrhi_residency_set_add_tlas(residency_set, tlas, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("residency add static: %s\n", err.message);
            return false;
        }
        static_resources_added_to_residency = true;
    }

    if (!history_resources_added_to_residency) {
        for (int i = 0; i < 2; ++i) {
            if (accum_textures[i]) {
                lrhi_residency_set_add_texture(residency_set, accum_textures[i], &err);
            }
            if (prev_textures[i]) {
                lrhi_residency_set_add_texture(residency_set, prev_textures[i], &err);
            }
        }

        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            printf("residency add history: %s\n", err.message);
            return false;
        }
        history_resources_added_to_residency = true;
    }

    return true;
}

void CornellPathtracerExample::reset_accumulation()
{
    accumulation_samples = 0;
    force_reset_accumulation = true;
}

void CornellPathtracerExample::record(LRHICommandList command_list,
                                      LRHITextureView target_view,
                                      int width,
                                      int height,
                                      LRHIResidencySet residency_set)
{
    if (!is_ready() || width <= 0 || height <= 0) {
        return;
    }

    if (!create_history_targets((uint32_t)width, (uint32_t)height, residency_set)) {
        return;
    }

    if (!ensure_residency(residency_set)) {
        return;
    }
    lrhi_residency_set_update(residency_set, nullptr);

    ImGuiIO& io = ImGui::GetIO();
    bool camera_changed = false;
    if (!io.WantCaptureMouse && io.MouseDown[0]) {
        camera_yaw += io.MouseDelta.x * 0.0055f;
        camera_pitch += io.MouseDelta.y * 0.0055f;
        camera_pitch = std::clamp(camera_pitch, -1.35f, 1.35f);
        camera_changed = (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
    }
    if (!io.WantCaptureMouse && io.MouseWheel != 0.0f) {
        camera_distance *= (1.0f - io.MouseWheel * 0.10f);
        camera_distance = std::clamp(camera_distance, 2.2f, 12.0f);
        camera_changed = true;
    }
    if (camera_changed) {
        reset_accumulation();
    }

    HMM_Vec3 center = HMM_V3(0.0f, 1.0f, 0.0f);
    HMM_Vec3 world_up = HMM_V3(0.0f, 1.0f, 0.0f);

    float cp = cosf(camera_pitch);
    float sp = sinf(camera_pitch);
    float cy = cosf(camera_yaw);
    float sy = sinf(camera_yaw);
    HMM_Vec3 orbit_offset = HMM_V3(cp * sy * camera_distance,
                                   sp * camera_distance,
                                   -cp * cy * camera_distance);
    HMM_Vec3 eye = HMM_AddV3(center, orbit_offset);
    HMM_Vec3 forward = HMM_NormV3(HMM_SubV3(center, eye));
    HMM_Vec3 right = HMM_NormV3(HMM_Cross(forward, world_up));
    HMM_Vec3 up = HMM_NormV3(HMM_Cross(right, forward));

    float aspect = trace_height > 0 ? (float)trace_width / (float)trace_height : 1.0f;
    float tan_half_fov = tanf(HMM_AngleDeg(55.0f) * 0.5f);

    LRHIError err = {};

    bool built_as_this_frame = false;
    if (!acceleration_structures_built) {
        LRHIAccelerationStructurePass as_pass = lrhi_acceleration_structure_pass_begin(command_list, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !as_pass) {
            return;
        }

        lrhi_acceleration_structure_pass_build_blas(as_pass, blas, scratch_buffer, 0, &err);
        lrhi_acceleration_structure_pass_barrier(as_pass, &err);
        lrhi_acceleration_structure_pass_build_tlas(as_pass, tlas, scratch_buffer, blas_scratch_size_aligned, &err);
        lrhi_acceleration_structure_pass_end(as_pass, &err);

        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            return;
        }
        acceleration_structures_built = true;
        built_as_this_frame = true;
    }

    LRHIComputePass compute_pass = lrhi_compute_pass_begin(command_list, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !compute_pass) {
        return;
    }

    if (built_as_this_frame) {
        lrhi_compute_pass_encoder_barrier(compute_pass, LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD, &err);
    }
    lrhi_compute_pass_set_pipeline(compute_pass, trace_pipeline, &err);

    uint32_t samples_before = accumulation_samples;
    uint32_t history_index = (uint32_t)active_history_index;

    for (int sample = 0; sample < samples_per_frame; ++sample) {
        uint32_t write_index = 1u - history_index;

        struct TracePush {
            uint32_t output_accum;
            uint32_t input_accum;
            uint32_t output_prev;
            uint32_t input_prev;

            uint32_t tlas;
            uint32_t width;
            uint32_t height;
            uint32_t accumulated_samples;

            uint32_t max_bounces;
            uint32_t frame_seed;
            uint32_t flags;
            uint32_t _pad0;

            float camera_position[4];
            float camera_forward[4];
            float camera_right[4];
            float camera_up_tan[4];

            float camera_aspect;
            float padding[3];
        } pc = {};

        pc.output_accum = accum_storage_handles[write_index];
        pc.input_accum = accum_sampled_handles[history_index];
        pc.output_prev = prev_storage_handles[write_index];
        pc.input_prev = prev_sampled_handles[history_index];

        pc.tlas = tlas_handle;
        pc.width = trace_width;
        pc.height = trace_height;
        pc.accumulated_samples = samples_before + (uint32_t)sample;

        pc.max_bounces = (uint32_t)std::max(max_bounces, 1);
        pc.frame_seed = (uint32_t)ImGui::GetFrameCount() + (uint32_t)sample * 977u;
        pc.flags = 0u;
        if (force_reset_accumulation && sample == 0) {
            pc.flags |= 1u;
        }

        pc.camera_position[0] = eye.X;
        pc.camera_position[1] = eye.Y;
        pc.camera_position[2] = eye.Z;
        pc.camera_position[3] = 0.0f;

        pc.camera_forward[0] = forward.X;
        pc.camera_forward[1] = forward.Y;
        pc.camera_forward[2] = forward.Z;
        pc.camera_forward[3] = 0.0f;

        pc.camera_right[0] = right.X;
        pc.camera_right[1] = right.Y;
        pc.camera_right[2] = right.Z;
        pc.camera_right[3] = 0.0f;

        pc.camera_up_tan[0] = up.X;
        pc.camera_up_tan[1] = up.Y;
        pc.camera_up_tan[2] = up.Z;
        pc.camera_up_tan[3] = tan_half_fov;

        pc.camera_aspect = aspect;

        lrhi_compute_pass_set_push_constants(compute_pass, &pc, sizeof(pc), &err);
        lrhi_compute_pass_dispatch(
            compute_pass,
            (trace_width + 7u) / 8u,
            (trace_height + 7u) / 8u,
            1,
            8,
            8,
            1,
            &err);

        if (sample + 1 < samples_per_frame) {
            lrhi_compute_pass_barrier(compute_pass, &err);
        }

        history_index = write_index;
    }

    lrhi_compute_pass_end(compute_pass, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        return;
    }

    active_history_index = (int)history_index;
    accumulation_samples = samples_before + (uint32_t)std::max(samples_per_frame, 1);
    force_reset_accumulation = false;

    LRHIRenderPassInfo rp_info = {};
    rp_info.color_attachments[0].texture_view = target_view;
    rp_info.color_attachments[0].load_action = LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
    rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
    rp_info.color_attachments[0].clear_color[0] = 0.0f;
    rp_info.color_attachments[0].clear_color[1] = 0.0f;
    rp_info.color_attachments[0].clear_color[2] = 0.0f;
    rp_info.color_attachments[0].clear_color[3] = 1.0f;
    rp_info.color_attachment_count = 1;
    rp_info.render_width = (uint32_t)width;
    rp_info.render_height = (uint32_t)height;

    LRHIRenderPass rp = lrhi_render_pass_begin(command_list, &rp_info, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !rp) {
        return;
    }

    lrhi_render_pass_encoder_barrier(rp, LUMINARY_RHI_RENDER_STAGE_COMPUTE, LUMINARY_RHI_RENDER_STAGE_FRAGMENT, &err);
    lrhi_render_pass_set_render_pipeline(rp, tonemap_pipeline, &err);
    lrhi_render_pass_set_viewport(rp, 0, 0, (uint32_t)width, (uint32_t)height, 0.0f, 1.0f, &err);
    lrhi_render_pass_set_scissor(rp, 0, 0, (uint32_t)width, (uint32_t)height, &err);

    struct TonemapPush {
        uint32_t accum_texture;
        uint32_t sampler;
        float exposure;
        uint32_t tonemap_mode;
    } tp = {};
    tp.accum_texture = accum_sampled_handles[active_history_index];
    tp.sampler = sampler_handle;
    tp.exposure = exposure;
    tp.tonemap_mode = (uint32_t)std::max(tonemap_mode, 0);

    lrhi_render_pass_set_push_constants(rp, &tp, sizeof(tp), &err);
    lrhi_render_pass_draw(rp, 3, 1, 0, 0, &err);
    lrhi_render_pass_end(rp, &err);
}

void CornellPathtracerExample::draw_ui()
{
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    ImGui::Begin("Cornell Pathtracer", nullptr, flags);
    ImGui::TextUnformatted("Compute RayQuery path tracing + history accumulation");
    if (show_help) {
        ImGui::TextUnformatted("Left-drag: orbit camera | Scroll: zoom");
        ImGui::TextUnformatted("Press Escape to return to menu.");
    }
    ImGui::Separator();

    bool new_half_resolution = half_resolution;
    if (ImGui::Checkbox("Half Resolution Trace", &new_half_resolution)) {
        half_resolution = new_half_resolution;
        reset_accumulation();
        trace_width = 0;
        trace_height = 0;
    }

    if (ImGui::SliderInt("Max Bounces", &max_bounces, 1, 6)) {
        reset_accumulation();
    }

    if (ImGui::SliderInt("Samples / Frame", &samples_per_frame, 1, 4)) {
        samples_per_frame = std::clamp(samples_per_frame, 1, 4);
    }

    ImGui::SliderFloat("Exposure", &exposure, 0.2f, 4.0f, "%.2f");

    const char* tonemap_items[] = { "ACES", "Reinhard" };
    if (ImGui::Combo("Tonemap", &tonemap_mode, tonemap_items, 2)) {
    }

    if (ImGui::Button("Reset Accumulation")) {
        reset_accumulation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera")) {
        camera_yaw = 0.0f;
        camera_pitch = 0.05f;
        camera_distance = 2.8f;
        reset_accumulation();
    }

    ImGui::Checkbox("Show Help", &show_help);

    ImGui::Text("Accumulated samples: %u", accumulation_samples);
    ImGui::Text("Trace resolution: %ux%u", trace_width, trace_height);
    ImGui::End();
}
