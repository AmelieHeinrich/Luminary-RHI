#include "tests/draw_helpers.h"
#include <cstring>
#include <string>

// Column-major identity transform (MTLPackedFloat4x3: 4 columns × 3 floats)
static void rt_make_translate(float out[12], float tx, float ty, float tz)
{
    out[0]  = 1.0f; out[1]  = 0.0f; out[2]  = 0.0f;  // col 0
    out[3]  = 0.0f; out[4]  = 1.0f; out[5]  = 0.0f;  // col 1
    out[6]  = 0.0f; out[7]  = 0.0f; out[8]  = 1.0f;  // col 2
    out[9]  = tx;   out[10] = ty;   out[11] = tz;     // col 3 (translation)
}

// Shared vertex/index data for a single triangle centered at origin
static const float kTriangleVertices[9] = {
     0.0f,  0.5f, 0.0f,   // v0 top
    -0.5f, -0.5f, 0.0f,   // v1 bottom-left
     0.5f, -0.5f, 0.0f,   // v2 bottom-right
};
static const uint32_t kTriangleIndices[3] = { 0, 1, 2 };

// ---------------------------------------------------------------------------
// Submit helper matching the pattern in raytracing_triangle.cpp
// ---------------------------------------------------------------------------

static bool rt_submit(LRHIDevice device,
                      LRHICommandQueue queue,
                      LRHICommandList  cmd,
                      LRHIFence        fence,
                      LRHIResidencySet rs,
                      std::string&     err_out)
{
    lrhi_command_queue_add_residency_set(queue, rs, nullptr);
    LRHIError err = {};
    lrhi_command_list_end(cmd, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        err_out = std::string("cmd end: ") + err.message;
        return false;
    }
    lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
    lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
    lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// Test 1: raytracing_non_opaque
// Non-opaque BLAS; shader discards hits in the red-dominant (w > 0.5) region,
// creating a hole near the top vertex.
// ---------------------------------------------------------------------------

class raytracing_non_opaque_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;

    LRHIDevice _device = nullptr;

    LRHIBuffer  _vertex_buffer  = nullptr;
    LRHIBuffer  _index_buffer   = nullptr;
    LRHIBuffer  _scratch_buffer = nullptr;

    LRHIBottomLevelAccelerationStructure _blas = nullptr;
    LRHITopLevelAccelerationStructure    _tlas = nullptr;

    LRHITexture     _output_texture = nullptr;
    LRHITextureView _output_view    = nullptr;

    LRHIShaderModule    _compute_shader = nullptr;
    LRHIComputePipeline _pipeline       = nullptr;

    LRHIResidencySet _rs = nullptr;

public:
    raytracing_non_opaque_test()
    {
        type        = test_type::texture;
        name        = "raytracing_non_opaque";
        source_path = "tests/golden/raytracing_non_opaque.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIDeviceInfo device_info = lrhi_get_device_info(device);
        if (!device_info.features.ray_tracing)
            return;

        LRHIError err = {};

        {
            LRHIBufferInfo bi = {};
            bi.size   = sizeof(kTriangleVertices);
            bi.stride = sizeof(float) * 3;
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
            lrhi_create_buffer(device, &bi, &_vertex_buffer, &err);
            void* ptr = lrhi_buffer_map(_vertex_buffer, &err);
            memcpy(ptr, kTriangleVertices, sizeof(kTriangleVertices));
            lrhi_buffer_unmap(_vertex_buffer);
        }
        {
            LRHIBufferInfo bi = {};
            bi.size   = sizeof(kTriangleIndices);
            bi.stride = sizeof(uint32_t);
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
            lrhi_create_buffer(device, &bi, &_index_buffer, &err);
            void* ptr = lrhi_buffer_map(_index_buffer, &err);
            memcpy(ptr, kTriangleIndices, sizeof(kTriangleIndices));
            lrhi_buffer_unmap(_index_buffer);
        }

        // Non-opaque geometry
        LRHIBLASGeometryInfo geom = {};
        geom.vertex_buffer = _vertex_buffer;
        geom.vertex_count  = 3;
        geom.index_buffer  = _index_buffer;
        geom.index_count   = 3;
        geom.opaque        = 0;

        LRHIBLASInfo blas_info = {};
        blas_info.geometry_type  = LUMINARY_RHI_BOTTOM_LEVEL_GEOMETRY_TYPE_TRIANGLES;
        blas_info.geometry_count = 1;
        blas_info.geometries     = &geom;
        lrhi_create_bottom_level_acceleration_structure(device, &blas_info, &_blas, &err);
        if (!_blas) return;

        LRHITLASInfo tlas_info = {};
        tlas_info.max_instance_count = 1;
        lrhi_create_top_level_acceleration_structure(device, &tlas_info, &_tlas, &err);
        if (!_tlas) return;

        float transform[12];
        rt_make_translate(transform, 0, 0, 0);

        LRHITLASInstanceInfo inst = {};
        inst.blas   = _blas;
        inst.opaque = 0;
        memcpy(inst.transform, transform, sizeof(transform));
        lrhi_add_top_level_acceleration_structure_instance(_tlas, &inst, &err);

        LRHIAccelerationStructureBufferSizes blas_sizes =
            lrhi_bottom_level_acceleration_structure_get_build_scratch_size(_blas, &err);
        LRHIAccelerationStructureBufferSizes tlas_sizes =
            lrhi_top_level_acceleration_structure_get_build_scratch_size(_tlas, &err);

        static constexpr uint64_t kAlign = 256;
        uint64_t blas_aligned   = (blas_sizes.build_scratch_size + kAlign - 1) & ~(kAlign - 1);
        uint64_t scratch_total  = blas_aligned + tlas_sizes.build_scratch_size;

        {
            LRHIBufferInfo bi = {};
            bi.size  = scratch_total;
            bi.stride = 1;
            bi.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ | LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
            lrhi_create_buffer(device, &bi, &_scratch_buffer, &err);
        }
        if (!_scratch_buffer) return;

        {
            LRHITextureInfo ti = {};
            ti.width = W; ti.height = H; ti.depth = 1;
            ti.mip_levels = 1; ti.array_layers = 1;
            ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            ti.usage      = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
            lrhi_create_texture(device, &ti, &_output_texture, &err);
        }
        _output_view = dh_make_view(device, _output_texture,
                                    LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/raytracing_non_opaque.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader = _compute_shader;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _output_texture, nullptr);
        lrhi_residency_set_add_buffer(_rs, _vertex_buffer,   nullptr);
        lrhi_residency_set_add_buffer(_rs, _index_buffer,    nullptr);
        lrhi_residency_set_add_buffer(_rs, _scratch_buffer,  nullptr);
        lrhi_residency_set_add_blas(_rs, _blas, nullptr);
        lrhi_residency_set_add_tlas(_rs, _tlas, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        LRHIDeviceInfo device_info = lrhi_get_device_info(_device);
        if (!device_info.features.ray_tracing)
            return {false, "ray tracing not supported on this device"};
        if (!_pipeline || !_output_texture || !_output_view || !_blas || !_tlas || !_scratch_buffer)
            return {false, "init failed"};

        LRHIError lerr = {};
        uint32_t output_index = lrhi_texture_view_get_bindless_index(_output_view, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get texture bindless index"};

        uint64_t tlas_index = lrhi_top_level_acceleration_structure_get_bindless_index(_tlas, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get tlas bindless index"};

        LRHIAccelerationStructureBufferSizes blas_sizes =
            lrhi_bottom_level_acceleration_structure_get_build_scratch_size(_blas, &lerr);
        static constexpr uint64_t kAlign = 256;
        uint64_t blas_aligned = (blas_sizes.build_scratch_size + kAlign - 1) & ~(kAlign - 1);

        LRHICommandQueue queue = nullptr;
        LRHIFence        fence = nullptr;
        LRHICommandList  cmd   = nullptr;
        lrhi_create_command_queue(_device, &queue, nullptr);
        lrhi_create_fence(_device, 0, &fence, nullptr);
        lrhi_create_command_list(queue, &cmd, nullptr);

        lerr = {};
        lrhi_command_list_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
            return {false, std::string("cmd begin: ") + lerr.message};
        }

        LRHIAccelerationStructurePass as_pass = lrhi_acceleration_structure_pass_begin(cmd, &lerr);
        lrhi_acceleration_structure_pass_build_blas(as_pass, _blas, _scratch_buffer, 0, &lerr);
        lrhi_acceleration_structure_pass_barrier(as_pass, &lerr);
        lrhi_acceleration_structure_pass_build_tlas(as_pass, _tlas, _scratch_buffer, blas_aligned, &lerr);
        lrhi_acceleration_structure_pass_end(as_pass, &lerr);

        LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
        lrhi_compute_pass_encoder_barrier(compute_pass, LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD, &lerr);
        lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
        struct PC { uint32_t output_texture; uint32_t tlas; } pc = { output_index, (uint32_t)tlas_index };
        lrhi_compute_pass_set_push_constants(compute_pass, &pc, sizeof(pc), &lerr);
        lrhi_compute_pass_dispatch(compute_pass, 16, 16, 1, 8, 8, 1, &lerr);
        lrhi_compute_pass_end(compute_pass, &lerr);

        std::string err_str;
        if (!rt_submit(_device, queue, cmd, fence, _rs, err_str)) {
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
            return {false, err_str};
        }
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);

        return dh_texture_test_result(_device, _output_texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view)    { lrhi_destroy_texture_view(_output_view);                          _output_view    = nullptr; }
        if (_pipeline)       { lrhi_destroy_compute_pipeline(_pipeline);                         _pipeline       = nullptr; }
        if (_compute_shader) { lrhi_destroy_shader_module(_compute_shader);                      _compute_shader = nullptr; }
        if (_rs)             { lrhi_destroy_residency_set(_rs);                                  _rs             = nullptr; }
        if (_tlas)           { lrhi_destroy_top_level_acceleration_structure(_tlas);             _tlas           = nullptr; }
        if (_blas)           { lrhi_destroy_bottom_level_acceleration_structure(_blas);          _blas           = nullptr; }
        if (_scratch_buffer) { lrhi_destroy_buffer(_scratch_buffer);                             _scratch_buffer = nullptr; }
        if (_index_buffer)   { lrhi_destroy_buffer(_index_buffer);                               _index_buffer   = nullptr; }
        if (_vertex_buffer)  { lrhi_destroy_buffer(_vertex_buffer);                              _vertex_buffer  = nullptr; }
        if (_output_texture) { lrhi_destroy_texture(_output_texture);                            _output_texture = nullptr; }
    }
};

REGISTER_TEST(raytracing_non_opaque_test);

// ---------------------------------------------------------------------------
// Shared base for the two multi-instance tests (3 translated copies of the
// same triangle BLAS arranged side-by-side)
// ---------------------------------------------------------------------------

struct rt_multi_instance_base : public test
{
    static constexpr uint32_t W               = 128;
    static constexpr uint32_t H               = 128;
    static constexpr uint32_t kInstanceCount  = 3;

    LRHIDevice _device = nullptr;

    LRHIBuffer  _vertex_buffer  = nullptr;
    LRHIBuffer  _index_buffer   = nullptr;
    LRHIBuffer  _scratch_buffer = nullptr;

    LRHIBottomLevelAccelerationStructure _blas = nullptr;
    LRHITopLevelAccelerationStructure    _tlas = nullptr;

    LRHITexture     _output_texture = nullptr;
    LRHITextureView _output_view    = nullptr;

    LRHIShaderModule    _compute_shader = nullptr;
    LRHIComputePipeline _pipeline       = nullptr;

    LRHIResidencySet _rs = nullptr;

    void init_common(LRHIDevice device, const char* shader_path)
    {
        _device = device;

        LRHIDeviceInfo device_info = lrhi_get_device_info(device);
        if (!device_info.features.ray_tracing)
            return;

        LRHIError err = {};

        {
            LRHIBufferInfo bi = {};
            bi.size   = sizeof(kTriangleVertices);
            bi.stride = sizeof(float) * 3;
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
            lrhi_create_buffer(device, &bi, &_vertex_buffer, &err);
            void* ptr = lrhi_buffer_map(_vertex_buffer, &err);
            memcpy(ptr, kTriangleVertices, sizeof(kTriangleVertices));
            lrhi_buffer_unmap(_vertex_buffer);
        }
        {
            LRHIBufferInfo bi = {};
            bi.size   = sizeof(kTriangleIndices);
            bi.stride = sizeof(uint32_t);
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
            lrhi_create_buffer(device, &bi, &_index_buffer, &err);
            void* ptr = lrhi_buffer_map(_index_buffer, &err);
            memcpy(ptr, kTriangleIndices, sizeof(kTriangleIndices));
            lrhi_buffer_unmap(_index_buffer);
        }

        LRHIBLASGeometryInfo geom = {};
        geom.vertex_buffer = _vertex_buffer;
        geom.vertex_count  = 3;
        geom.index_buffer  = _index_buffer;
        geom.index_count   = 3;
        geom.opaque        = 1;

        LRHIBLASInfo blas_info = {};
        blas_info.geometry_type  = LUMINARY_RHI_BOTTOM_LEVEL_GEOMETRY_TYPE_TRIANGLES;
        blas_info.geometry_count = 1;
        blas_info.geometries     = &geom;
        lrhi_create_bottom_level_acceleration_structure(device, &blas_info, &_blas, &err);
        if (!_blas) return;

        LRHITLASInfo tlas_info = {};
        tlas_info.max_instance_count = kInstanceCount;
        lrhi_create_top_level_acceleration_structure(device, &tlas_info, &_tlas, &err);
        if (!_tlas) return;

        // Three instances: left (-0.7), center (0), right (+0.7), scaled to 0.3
        static const float kTranslations[kInstanceCount] = { -0.7f, 0.0f, 0.7f };
        for (uint32_t i = 0; i < kInstanceCount; ++i) {
            float transform[12];
            // Scale 0.3 and translate along X: column-major 4×3
            transform[0] = 0.3f; transform[1]  = 0.0f; transform[2]  = 0.0f;  // col 0
            transform[3] = 0.0f; transform[4]  = 0.3f; transform[5]  = 0.0f;  // col 1
            transform[6] = 0.0f; transform[7]  = 0.0f; transform[8]  = 0.3f;  // col 2
            transform[9] = kTranslations[i]; transform[10] = 0.0f; transform[11] = 0.0f;  // col 3

            LRHITLASInstanceInfo inst = {};
            inst.blas    = _blas;
            inst.user_id = i;
            inst.opaque  = 1;
            memcpy(inst.transform, transform, sizeof(transform));
            lrhi_add_top_level_acceleration_structure_instance(_tlas, &inst, &err);
        }

        LRHIAccelerationStructureBufferSizes blas_sizes =
            lrhi_bottom_level_acceleration_structure_get_build_scratch_size(_blas, &err);
        LRHIAccelerationStructureBufferSizes tlas_sizes =
            lrhi_top_level_acceleration_structure_get_build_scratch_size(_tlas, &err);

        static constexpr uint64_t kAlign = 256;
        uint64_t blas_aligned  = (blas_sizes.build_scratch_size + kAlign - 1) & ~(kAlign - 1);
        uint64_t scratch_total = blas_aligned + tlas_sizes.build_scratch_size;

        {
            LRHIBufferInfo bi = {};
            bi.size   = scratch_total;
            bi.stride = 1;
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ | LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
            lrhi_create_buffer(device, &bi, &_scratch_buffer, &err);
        }
        if (!_scratch_buffer) return;

        {
            LRHITextureInfo ti = {};
            ti.width = W; ti.height = H; ti.depth = 1;
            ti.mip_levels = 1; ti.array_layers = 1;
            ti.format     = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            ti.usage      = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            ti.dimensions = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
            lrhi_create_texture(device, &ti, &_output_texture, &err);
        }
        _output_view = dh_make_view(device, _output_texture,
                                    LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string err_str;
        std::string src = dh_read_shader_file(shader_path);
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader = _compute_shader;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _output_texture, nullptr);
        lrhi_residency_set_add_buffer(_rs, _vertex_buffer,   nullptr);
        lrhi_residency_set_add_buffer(_rs, _index_buffer,    nullptr);
        lrhi_residency_set_add_buffer(_rs, _scratch_buffer,  nullptr);
        lrhi_residency_set_add_blas(_rs, _blas, nullptr);
        lrhi_residency_set_add_tlas(_rs, _tlas, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run_common(bool bake_mode)
    {
        LRHIDeviceInfo device_info = lrhi_get_device_info(_device);
        if (!device_info.features.ray_tracing)
            return {false, "ray tracing not supported on this device"};
        if (!_pipeline || !_output_texture || !_output_view || !_blas || !_tlas || !_scratch_buffer)
            return {false, "init failed"};

        LRHIError lerr = {};
        uint32_t output_index = lrhi_texture_view_get_bindless_index(_output_view, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get texture bindless index"};

        uint64_t tlas_index = lrhi_top_level_acceleration_structure_get_bindless_index(_tlas, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
            return {false, "failed to get tlas bindless index"};

        LRHIAccelerationStructureBufferSizes blas_sizes =
            lrhi_bottom_level_acceleration_structure_get_build_scratch_size(_blas, &lerr);
        static constexpr uint64_t kAlign = 256;
        uint64_t blas_aligned = (blas_sizes.build_scratch_size + kAlign - 1) & ~(kAlign - 1);

        LRHICommandQueue queue = nullptr;
        LRHIFence        fence = nullptr;
        LRHICommandList  cmd   = nullptr;
        lrhi_create_command_queue(_device, &queue, nullptr);
        lrhi_create_fence(_device, 0, &fence, nullptr);
        lrhi_create_command_list(queue, &cmd, nullptr);

        lerr = {};
        lrhi_command_list_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
            return {false, std::string("cmd begin: ") + lerr.message};
        }

        LRHIAccelerationStructurePass as_pass = lrhi_acceleration_structure_pass_begin(cmd, &lerr);
        lrhi_acceleration_structure_pass_build_blas(as_pass, _blas, _scratch_buffer, 0, &lerr);
        lrhi_acceleration_structure_pass_barrier(as_pass, &lerr);
        lrhi_acceleration_structure_pass_build_tlas(as_pass, _tlas, _scratch_buffer, blas_aligned, &lerr);
        lrhi_acceleration_structure_pass_end(as_pass, &lerr);

        LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
        lrhi_compute_pass_encoder_barrier(compute_pass, LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD, &lerr);
        lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
        struct PC { uint32_t output_texture; uint32_t tlas; } pc = { output_index, (uint32_t)tlas_index };
        lrhi_compute_pass_set_push_constants(compute_pass, &pc, sizeof(pc), &lerr);
        lrhi_compute_pass_dispatch(compute_pass, 16, 16, 1, 8, 8, 1, &lerr);
        lrhi_compute_pass_end(compute_pass, &lerr);

        std::string err_str;
        if (!rt_submit(_device, queue, cmd, fence, _rs, err_str)) {
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
            return {false, err_str};
        }
        lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);

        return dh_texture_test_result(_device, _output_texture, name, source_path, bake_mode);
    }

    void cleanup_common()
    {
        if (_output_view)    { lrhi_destroy_texture_view(_output_view);                          _output_view    = nullptr; }
        if (_pipeline)       { lrhi_destroy_compute_pipeline(_pipeline);                         _pipeline       = nullptr; }
        if (_compute_shader) { lrhi_destroy_shader_module(_compute_shader);                      _compute_shader = nullptr; }
        if (_rs)             { lrhi_destroy_residency_set(_rs);                                  _rs             = nullptr; }
        if (_tlas)           { lrhi_destroy_top_level_acceleration_structure(_tlas);             _tlas           = nullptr; }
        if (_blas)           { lrhi_destroy_bottom_level_acceleration_structure(_blas);          _blas           = nullptr; }
        if (_scratch_buffer) { lrhi_destroy_buffer(_scratch_buffer);                             _scratch_buffer = nullptr; }
        if (_index_buffer)   { lrhi_destroy_buffer(_index_buffer);                               _index_buffer   = nullptr; }
        if (_vertex_buffer)  { lrhi_destroy_buffer(_vertex_buffer);                              _vertex_buffer  = nullptr; }
        if (_output_texture) { lrhi_destroy_texture(_output_texture);                            _output_texture = nullptr; }
    }
};

// ---------------------------------------------------------------------------
// Test 2: raytracing_multi_instance
// Three translated+scaled copies of the same BLAS; each rendered with
// barycentric colors (red/green/blue vertex gradient).
// ---------------------------------------------------------------------------

class raytracing_multi_instance_test : public rt_multi_instance_base
{
public:
    raytracing_multi_instance_test()
    {
        type        = test_type::texture;
        name        = "raytracing_multi_instance";
        source_path = "tests/golden/raytracing_multi_instance.png";
    }

    void init(LRHIDevice device) override
    {
        init_common(device, "shaders/tests/raytracing_multi_instance.hlsl");
    }

    test_result run(bool bake_mode) override { return run_common(bake_mode); }
    void cleanup() override { cleanup_common(); }
};

REGISTER_TEST(raytracing_multi_instance_test);

// ---------------------------------------------------------------------------
// Test 3: raytracing_instance_id
// Same three-instance setup; each instance colored by its user_id (0=red,
// 1=green, 2=blue) via rq.CommittedInstanceID().
// ---------------------------------------------------------------------------

class raytracing_instance_id_test : public rt_multi_instance_base
{
public:
    raytracing_instance_id_test()
    {
        type        = test_type::texture;
        name        = "raytracing_instance_id";
        source_path = "tests/golden/raytracing_instance_id.png";
    }

    void init(LRHIDevice device) override
    {
        init_common(device, "shaders/tests/raytracing_instance_id.hlsl");
    }

    test_result run(bool bake_mode) override { return run_common(bake_mode); }
    void cleanup() override { cleanup_common(); }
};

REGISTER_TEST(raytracing_instance_id_test);
