#include "tests/draw_helpers.h"
#include <cstring>
#include <string>

// AABB buffer layout matching MTLAxisAlignedBoundingBox: float3 min + float3 max (24 bytes)
struct AABBEntry { float min_x, min_y, min_z, max_x, max_y, max_z; };

class raytracing_aabb_test : public test
{
    static constexpr uint32_t W = 128;
    static constexpr uint32_t H = 128;

    LRHIDevice _device = nullptr;

    LRHIBuffer _aabb_buffer   = nullptr;
    LRHIBuffer _scratch_buffer = nullptr;

    LRHIBottomLevelAccelerationStructure _blas = nullptr;
    LRHITopLevelAccelerationStructure    _tlas = nullptr;

    LRHITexture     _output_texture = nullptr;
    LRHITextureView _output_view    = nullptr;

    LRHIShaderModule    _compute_shader = nullptr;
    LRHIComputePipeline _pipeline       = nullptr;

    LRHIResidencySet _rs = nullptr;

public:
    raytracing_aabb_test()
    {
        type        = test_type::texture;
        name        = "raytracing_aabb";
        source_path = "tests/golden/raytracing_aabb.png";
    }

    void init(LRHIDevice device) override
    {
        _device = device;

        LRHIDeviceInfo device_info = lrhi_get_device_info(device);
        if (!device_info.features.ray_tracing)
            return;

        LRHIError err = {};

        // AABB covering the center of the view: XY -0.5..0.5, Z -0.5..0.5
        static const AABBEntry kAABB = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
        {
            LRHIBufferInfo bi = {};
            bi.size   = sizeof(AABBEntry);
            bi.stride = sizeof(AABBEntry);
            bi.usage  = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_STAGING | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
            lrhi_create_buffer(device, &bi, &_aabb_buffer, &err);
            void* ptr = lrhi_buffer_map(_aabb_buffer, &err);
            memcpy(ptr, &kAABB, sizeof(kAABB));
            lrhi_buffer_unmap(_aabb_buffer);
        }

        // BLAS with procedural AABB geometry
        LRHIBLASGeometryInfo geom = {};
        geom.opaque              = 0; // non-opaque so rq.Proceed() fires CandidateType
        geom.aabbs.aabb_buffer   = _aabb_buffer;
        geom.aabbs.aabb_offset   = 0;
        geom.aabbs.aabb_count    = 1;
        geom.aabbs.aabb_stride   = sizeof(AABBEntry);

        LRHIBLASInfo blas_info = {};
        blas_info.allow_update  = 0;
        blas_info.geometry_type  = LUMINARY_RHI_BOTTOM_LEVEL_GEOMETRY_TYPE_PROCEDURAL_AABBS;
        blas_info.geometry_count = 1;
        blas_info.geometries     = &geom;
        lrhi_create_bottom_level_acceleration_structure(device, &blas_info, &_blas, &err);
        if (!_blas) return;

        // TLAS
        LRHITLASInfo tlas_info = {};
        tlas_info.max_instance_count = 1;
        lrhi_create_top_level_acceleration_structure(device, &tlas_info, &_tlas, &err);
        if (!_tlas) return;

        static const float kIdentity[12] = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f,
        };
        LRHITLASInstanceInfo inst = {};
        inst.blas    = _blas;
        inst.user_id = 0;
        inst.opaque  = 0;
        memcpy(inst.transform, kIdentity, sizeof(kIdentity));
        lrhi_add_top_level_acceleration_structure_instance(_tlas, &inst, &err);

        // Scratch buffer
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

        // Output texture
        {
            LRHITextureInfo ti = {};
            ti.width        = W;
            ti.height       = H;
            ti.depth        = 1;
            ti.mip_levels   = 1;
            ti.array_layers = 1;
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

        // Compute pipeline
        std::string err_str;
        std::string src = dh_read_shader_file("shaders/tests/raytracing_aabb.hlsl");
        if (src.empty()) return;
        auto [bc, sz] = dh_compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "CSMain");
        if (!bc) return;
        _compute_shader = dh_make_module(device, bc, sz, LUMINARY_RHI_SHADER_STAGE_COMPUTE, "CSMain", err_str);
        if (!_compute_shader) return;

        LRHIComputePipelineInfo pi = {};
        pi.compute_shader             = _compute_shader;
        pi.supports_indirect_commands = 0;
        lrhi_create_compute_pipeline(device, &pi, &_pipeline, &err);

        // Residency set
        lrhi_create_residency_set(device, &_rs, &err);
        lrhi_residency_set_add_texture(_rs, _output_texture, nullptr);
        lrhi_residency_set_add_buffer(_rs, _aabb_buffer,     nullptr);
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

        LRHIAccelerationStructureBufferSizes tlas_sizes =
            lrhi_top_level_acceleration_structure_get_build_scratch_size(_tlas, &lerr);

        LRHIAccelerationStructurePass as_pass = lrhi_acceleration_structure_pass_begin(cmd, &lerr);
        lrhi_acceleration_structure_pass_build_blas(as_pass, _blas, _scratch_buffer, 0, &lerr);
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
        lrhi_command_queue_wait(queue, fence, 1, 5000000000ULL, nullptr);
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
        if (_blas)           { lrhi_destroy_bottom_level_acceleration_structure(_blas);            _blas           = nullptr; }
        if (_scratch_buffer) { lrhi_destroy_buffer(_scratch_buffer);                               _scratch_buffer = nullptr; }
        if (_aabb_buffer)    { lrhi_destroy_buffer(_aabb_buffer);                                  _aabb_buffer    = nullptr; }
        if (_output_texture) { lrhi_destroy_texture(_output_texture);                              _output_texture = nullptr; }
    }
};

REGISTER_TEST(raytracing_aabb_test);
