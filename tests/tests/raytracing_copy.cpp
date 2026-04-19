#include "tests/draw_helpers.h"
#include <cstring>
#include <string>

// Builds a triangle BLAS, copies it into an identical-sized destination, then renders
// from the copy. The result must match the standard triangle golden image.
class raytracing_copy_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;

    LRHIDevice _device = nullptr;

    LRHIBuffer _vertex_buffer  = nullptr;
    LRHIBuffer _index_buffer   = nullptr;
    LRHIBuffer _scratch_buffer = nullptr;

    LRHIBottomLevelAccelerationStructure _blas     = nullptr;
    LRHIBottomLevelAccelerationStructure _blas_dst = nullptr;
    LRHITopLevelAccelerationStructure    _tlas     = nullptr;

    LRHITexture     _output_texture = nullptr;
    LRHITextureView _output_view    = nullptr;

    LRHIShaderModule    _compute_shader = nullptr;
    LRHIComputePipeline _pipeline       = nullptr;

    LRHIResidencySet _rs = nullptr;

public:
    raytracing_copy_test()
    {
        type        = test_type::texture;
        name        = "raytracing_copy";
        source_path = "tests/golden/raytracing_copy.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIDeviceInfo device_info = lrhi_get_device_info(device);
        if (!device_info.features.ray_tracing)
            return;

        LRHIError err = {};

        static const float kVertices[9] = {
             0.0f,  0.5f, 0.0f,
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
        };
        {
            LRHIBufferInfo bi = {};
            bi.size   = sizeof(kVertices);
            bi.stride = sizeof(float) * 3;
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
            lrhi_create_buffer(device, &bi, &_vertex_buffer, &err);
            void* ptr = lrhi_buffer_map(_vertex_buffer, &err);
            memcpy(ptr, kVertices, sizeof(kVertices));
            lrhi_buffer_unmap(_vertex_buffer);
        }

        static const uint32_t kIndices[3] = { 0, 1, 2 };
        {
            LRHIBufferInfo bi = {};
            bi.size   = sizeof(kIndices);
            bi.stride = sizeof(uint32_t);
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
            lrhi_create_buffer(device, &bi, &_index_buffer, &err);
            void* ptr = lrhi_buffer_map(_index_buffer, &err);
            memcpy(ptr, kIndices, sizeof(kIndices));
            lrhi_buffer_unmap(_index_buffer);
        }

        LRHIBLASGeometryInfo geom = {};
        geom.opaque                  = 1;
        geom.triangles.vertex_buffer = _vertex_buffer;
        geom.triangles.vertex_offset = 0;
        geom.triangles.vertex_count  = 3;
        geom.triangles.index_buffer  = _index_buffer;
        geom.triangles.index_offset  = 0;
        geom.triangles.index_count   = 3;

        LRHIBLASInfo blas_info = {};
        blas_info.allow_update   = 0;
        blas_info.geometry_type  = LUMINARY_RHI_BOTTOM_LEVEL_GEOMETRY_TYPE_TRIANGLES;
        blas_info.geometry_count = 1;
        blas_info.geometries     = &geom;

        // Create src and dst BLAS with the same descriptor (same size)
        lrhi_create_bottom_level_acceleration_structure(device, &blas_info, &_blas,     &err);
        lrhi_create_bottom_level_acceleration_structure(device, &blas_info, &_blas_dst, &err);
        if (!_blas || !_blas_dst) return;

        LRHITLASInfo tlas_info = {};
        tlas_info.max_instance_count = 1;
        lrhi_create_top_level_acceleration_structure(device, &tlas_info, &_tlas, &err);
        if (!_tlas) return;

        static const float kIdentity[12] = { 1,0,0, 0,1,0, 0,0,1, 0,0,0 };
        LRHITLASInstanceInfo inst = {};
        inst.blas   = _blas_dst; // render from the copy
        inst.user_id = 0;
        inst.opaque  = 1;
        memcpy(inst.transform, kIdentity, sizeof(kIdentity));
        lrhi_add_top_level_acceleration_structure_instance(_tlas, &inst, &err);

        LRHIAccelerationStructureBufferSizes blas_sizes =
            lrhi_bottom_level_acceleration_structure_get_build_scratch_size(_blas, &err);
        LRHIAccelerationStructureBufferSizes tlas_sizes =
            lrhi_top_level_acceleration_structure_get_build_scratch_size(_tlas, &err);

        static constexpr uint64_t kAlign = 256;
        uint64_t blas_scratch_aligned = (blas_sizes.build_scratch_size + kAlign - 1) & ~(kAlign - 1);
        uint64_t scratch_total        = blas_scratch_aligned + tlas_sizes.build_scratch_size;
        {
            LRHIBufferInfo bi = {};
            bi.size   = scratch_total ? scratch_total : 256;
            bi.stride = 1;
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_SHADER_READ | LUMINARY_RHI_BUFFER_USAGE_SHADER_WRITE);
            lrhi_create_buffer(device, &bi, &_scratch_buffer, &err);
        }
        if (!_scratch_buffer) return;

        {
            LRHITextureInfo ti = {};
            ti.width        = W; ti.height = H; ti.depth = 1;
            ti.mip_levels   = 1; ti.array_layers = 1;
            ti.format       = LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
            ti.usage        = (LRHITextureUsage)(LUMINARY_RHI_TEXTURE_USAGE_STORAGE | LUMINARY_RHI_TEXTURE_USAGE_SAMPLED);
            ti.dimensions   = LUMINARY_RHI_TEXTURE_DIMENSIONS_2D;
            lrhi_create_texture(device, &ti, &_output_texture, &err);
        }
        _output_view = dh_make_view(device, _output_texture,
                                    LUMINARY_RHI_TEXTURE_USAGE_STORAGE,
                                    LUMINARY_RHI_TEXTURE_DIMENSIONS_2D,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_MIPS,
                                    0, LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS);

        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/raytracing_triangle.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader             = _compute_shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _output_texture, nullptr);
        lrhi_residency_set_add_buffer(_rs, _vertex_buffer,   nullptr);
        lrhi_residency_set_add_buffer(_rs, _index_buffer,    nullptr);
        lrhi_residency_set_add_buffer(_rs, _scratch_buffer,  nullptr);
        lrhi_residency_set_add_blas(_rs, _blas,     nullptr);
        lrhi_residency_set_add_blas(_rs, _blas_dst, nullptr);
        lrhi_residency_set_add_tlas(_rs, _tlas, nullptr);
        lrhi_residency_set_update(_rs, nullptr);
    }

    test_result run(bool bake_mode) override
    {
        LRHIDeviceInfo device_info = lrhi_get_device_info(_device);
        if (!device_info.features.ray_tracing)
            return {false, "ray tracing not supported on this device"};

        if (!_pipeline || !_output_texture || !_output_view || !_blas || !_blas_dst || !_tlas || !_scratch_buffer)
            return {false, "init failed"};

        LRHIError lerr = {};
        uint32_t output_index = lrhi_texture_view_get_bindless_index(_output_view, &lerr);
        uint64_t tlas_index   = lrhi_top_level_acceleration_structure_get_bindless_index(_tlas, &lerr);

        LRHIAccelerationStructureBufferSizes blas_sizes =
            lrhi_bottom_level_acceleration_structure_get_build_scratch_size(_blas, &lerr);

        static constexpr uint64_t kAlign = 256;
        uint64_t blas_scratch_aligned = (blas_sizes.build_scratch_size + kAlign - 1) & ~(kAlign - 1);

        LRHICommandQueue queue = nullptr;
        LRHIFence        fence = nullptr;
        LRHICommandList  cmd   = nullptr;
        lrhi_create_command_queue(_device, &queue, nullptr);
        lrhi_create_fence(_device, 0, &fence, nullptr);
        lrhi_create_command_list(queue, &cmd, nullptr);
        lrhi_command_queue_add_residency_set(queue, _rs, nullptr);

        lerr = {};
        lrhi_command_list_begin(cmd, &lerr);
        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
            return {false, std::string("cmd begin: ") + lerr.message};
        }

        LRHIAccelerationStructurePass as_pass = lrhi_acceleration_structure_pass_begin(cmd, &lerr);
        lrhi_acceleration_structure_pass_build_blas(as_pass, _blas, _scratch_buffer, 0, &lerr);
        lrhi_acceleration_structure_pass_barrier(as_pass, &lerr);
        // Copy built BLAS into destination
        lrhi_acceleration_structure_pass_copy_blas(as_pass, _blas, _blas_dst, &lerr);
        lrhi_acceleration_structure_pass_barrier(as_pass, &lerr);
        lrhi_acceleration_structure_pass_build_tlas(as_pass, _tlas, _scratch_buffer, blas_scratch_aligned, &lerr);
        lrhi_acceleration_structure_pass_end(as_pass, &lerr);

        LRHIComputePass compute_pass = lrhi_compute_pass_begin(cmd, &lerr);
        lrhi_compute_pass_encoder_barrier(compute_pass, LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD, &lerr);
        lrhi_compute_pass_set_pipeline(compute_pass, _pipeline, &lerr);
        struct PushConstants { uint32_t output_texture; uint32_t tlas; } pc = { output_index, (uint32_t)tlas_index };
        lrhi_compute_pass_set_push_constants(compute_pass, &pc, sizeof(pc), &lerr);
        lrhi_compute_pass_dispatch(compute_pass, 16, 16, 1, 8, 8, 1, &lerr);
        lrhi_compute_pass_end(compute_pass, &lerr);

        if (lerr.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_command_list_end(cmd, nullptr);
            lrhi_destroy_command_list(cmd); lrhi_destroy_fence(fence); lrhi_destroy_command_queue(queue);
            return {false, std::string("pass error: ") + lerr.message};
        }

        lrhi_command_list_end(cmd, &lerr);
        lrhi_command_queue_submit(queue, &cmd, 1, fence, 1, nullptr, 0, nullptr);
        lrhi_fence_wait(fence, 1, 5000000000ULL, nullptr);
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);

        return dh_texture_test_result(_device, _output_texture, name, source_path, bake_mode);
    }

    void cleanup() override
    {
        if (_output_view)    { lrhi_destroy_texture_view(_output_view);                            _output_view    = nullptr; }
        if (_pipeline)       { lrhi_destroy_compute_pipeline(_pipeline);                           _pipeline       = nullptr; }
        if (_compute_shader) { lrhi_destroy_shader_module(_compute_shader);                        _compute_shader = nullptr; }
        if (_rs)             { lrhi_destroy_residency_set(_rs);                                    _rs             = nullptr; }
        if (_tlas)           { lrhi_destroy_top_level_acceleration_structure(_tlas);               _tlas           = nullptr; }
        if (_blas_dst)       { lrhi_destroy_bottom_level_acceleration_structure(_blas_dst);        _blas_dst       = nullptr; }
        if (_blas)           { lrhi_destroy_bottom_level_acceleration_structure(_blas);            _blas           = nullptr; }
        if (_scratch_buffer) { lrhi_destroy_buffer(_scratch_buffer);                               _scratch_buffer = nullptr; }
        if (_index_buffer)   { lrhi_destroy_buffer(_index_buffer);                                 _index_buffer   = nullptr; }
        if (_vertex_buffer)  { lrhi_destroy_buffer(_vertex_buffer);                                _vertex_buffer  = nullptr; }
        if (_output_texture) { lrhi_destroy_texture(_output_texture);                              _output_texture = nullptr; }
    }
};

REGISTER_TEST(raytracing_copy_test);
