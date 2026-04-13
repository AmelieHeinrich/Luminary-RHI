#include "luminary_rhi.h"
#include "tests/test.h"
#include <luminary_shader_compiler.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_shader_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string src(size, '\0');
    fread(src.data(), 1, size, f);
    fclose(f);
    return src;
}

// Compile HLSL source to METALLIB (MSC path). Returns {bytecode, size} or {nullptr, 0}.
static std::pair<uint8_t*, uint64_t> compile_stage(const std::string& source,
                                                    LuminaryShaderStage stage,
                                                    const char* entry_point)
{
    LuminaryShaderCompilerOptions opts = {};
    opts.shading_language              = LUMINARY_SHADING_LANGUAGE_HLSL;
    opts.bytecode                      = LUMINARY_SHADING_BYTECODE_METALLIB;
    opts.shader_stage                  = stage;
    strncpy(opts.entry_point, entry_point, sizeof(opts.entry_point) - 1);
    opts.source_code      = const_cast<char*>(source.data());
    opts.source_code_size = source.size();

    uint64_t size     = 0;
    uint8_t* bytecode = luminary_compile_shader(&opts, &size);
    return {bytecode, size};
}

// ---------------------------------------------------------------------------
// Compilation tests
// ---------------------------------------------------------------------------

class compile_empty_entry_points_test : public test
{
public:
    compile_empty_entry_points_test()
    {
        type        = test_type::validation;
        name        = "compile_empty_entry_points";
        source_path = nullptr;
    }

    void init(LRHIDevice /*device*/) override {}

    test_result run(bool /*bake_mode*/) override
    {
        std::string src = read_shader_file("shaders/tests/compile_tests/empty_entry_points.hlsl");
        if (src.empty())
            return {false, "failed to read empty_entry_points.hlsl"};

        struct Stage { LuminaryShaderStage stage; const char* entry; };
        Stage stages[] = {
            { LUMINARY_SHADER_STAGE_VERTEX,   "VSMain" },
            { LUMINARY_SHADER_STAGE_FRAGMENT, "PSMain" },
            { LUMINARY_SHADER_STAGE_COMPUTE,  "CSMain" },
            { LUMINARY_SHADER_STAGE_TASK,     "ASMain" },
            { LUMINARY_SHADER_STAGE_MESH,     "MSMain" },
        };

        for (auto& s : stages)
        {
            auto [bytecode, size] = compile_stage(src, s.stage, s.entry);
            if (!bytecode)
                return {false, std::string("compilation failed for entry point: ") + s.entry};
            free(bytecode);
        }

        return {true, ""};
    }

    void cleanup() override {}
};

REGISTER_TEST(compile_empty_entry_points_test);

// ---------------------------------------------------------------------------

class compile_draw_id_test : public test
{
public:
    compile_draw_id_test()
    {
        type        = test_type::validation;
        name        = "compile_draw_id";
        source_path = nullptr;
    }

    void init(LRHIDevice /*device*/) override {}

    test_result run(bool /*bake_mode*/) override
    {
        std::string src = read_shader_file("shaders/tests/compile_tests/draw_id.hlsl");
        if (src.empty())
            return {false, "failed to read draw_id.hlsl"};

        auto [bytecode, size] = compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "main");
        if (!bytecode)
            return {false, "compilation failed for draw_id.hlsl"};
        free(bytecode);

        return {true, ""};
    }

    void cleanup() override {}
};

REGISTER_TEST(compile_draw_id_test);

// ---------------------------------------------------------------------------

class compile_bindless_load_test : public test
{
public:
    compile_bindless_load_test()
    {
        type        = test_type::validation;
        name        = "compile_bindless_load";
        source_path = nullptr;
    }

    void init(LRHIDevice /*device*/) override {}

    test_result run(bool /*bake_mode*/) override
    {
        std::string src = read_shader_file("shaders/tests/compile_tests/bindless_load.hlsl");
        if (src.empty())
            return {false, "failed to read bindless_load.hlsl"};

        auto [bytecode, size] = compile_stage(src, LUMINARY_SHADER_STAGE_COMPUTE, "main");
        if (!bytecode)
            return {false, "compilation failed for bindless_load.hlsl"};
        free(bytecode);

        return {true, ""};
    }

    void cleanup() override {}
};

REGISTER_TEST(compile_bindless_load_test);

// ---------------------------------------------------------------------------
// Shader module tests
// ---------------------------------------------------------------------------

// Helper: compile a stage from empty_entry_points.hlsl, create a shader module,
// verify info round-trips, then destroy it.
static test_result run_shader_module_test(LRHIDevice device,
                                          LuminaryShaderStage compile_stage_enum,
                                          LRHIShaderStage      rhi_stage,
                                          const char*          entry_point)
{
    std::string src = read_shader_file("shaders/tests/compile_tests/empty_entry_points.hlsl");
    if (src.empty())
        return {false, "failed to read empty_entry_points.hlsl"};

    auto [bytecode, size] = compile_stage(src, compile_stage_enum, entry_point);
    if (!bytecode)
        return {false, std::string("compilation failed for entry point: ") + entry_point};

    LRHIShaderModuleInfo info = {};
    info.stage       = rhi_stage;
    info.entry_point = entry_point;
    info.code        = reinterpret_cast<const uint32_t*>(bytecode);
    info.code_size   = static_cast<uint32_t>(size);

    LRHIShaderModule module = nullptr;
    LRHIError        err    = {};
    lrhi_create_shader_module(device, &info, &module, &err);
    free(bytecode);

    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR)
        return {false, std::string("lrhi_create_shader_module error: ") + err.message};
    if (!module)
        return {false, "lrhi_create_shader_module returned null module"};

    LRHIShaderModuleInfo out_info = {};
    lrhi_get_shader_module_info(module, &out_info);

    if (out_info.stage != rhi_stage)
        return {false, "shader module stage mismatch"};
    if (!out_info.entry_point || strcmp(out_info.entry_point, entry_point) != 0)
        return {false, "shader module entry_point mismatch"};

    lrhi_destroy_shader_module(module);
    return {true, ""};
}

// ---------------------------------------------------------------------------

class shader_module_vertex_test : public test
{
    LRHIDevice _device = nullptr;
public:
    shader_module_vertex_test()
    {
        type        = test_type::validation;
        name        = "shader_module_vertex";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override { _device = device; }

    test_result run(bool /*bake_mode*/) override
    {
        return run_shader_module_test(_device,
                                      LUMINARY_SHADER_STAGE_VERTEX,
                                      LUMINARY_RHI_SHADER_STAGE_VERTEX,
                                      "VSMain");
    }

    void cleanup() override {}
};

REGISTER_TEST(shader_module_vertex_test);

// ---------------------------------------------------------------------------

class shader_module_fragment_test : public test
{
    LRHIDevice _device = nullptr;
public:
    shader_module_fragment_test()
    {
        type        = test_type::validation;
        name        = "shader_module_fragment";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override { _device = device; }

    test_result run(bool /*bake_mode*/) override
    {
        return run_shader_module_test(_device,
                                      LUMINARY_SHADER_STAGE_FRAGMENT,
                                      LUMINARY_RHI_SHADER_STAGE_FRAGMENT,
                                      "PSMain");
    }

    void cleanup() override {}
};

REGISTER_TEST(shader_module_fragment_test);

// ---------------------------------------------------------------------------

class shader_module_compute_test : public test
{
    LRHIDevice _device = nullptr;
public:
    shader_module_compute_test()
    {
        type        = test_type::validation;
        name        = "shader_module_compute";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override { _device = device; }

    test_result run(bool /*bake_mode*/) override
    {
        return run_shader_module_test(_device,
                                      LUMINARY_SHADER_STAGE_COMPUTE,
                                      LUMINARY_RHI_SHADER_STAGE_COMPUTE,
                                      "CSMain");
    }

    void cleanup() override {}
};

REGISTER_TEST(shader_module_compute_test);

// ---------------------------------------------------------------------------

class shader_module_task_test : public test
{
    LRHIDevice _device = nullptr;
public:
    shader_module_task_test()
    {
        type        = test_type::validation;
        name        = "shader_module_task";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override { _device = device; }

    test_result run(bool /*bake_mode*/) override
    {
        if (!lrhi_get_device_info(_device).features.mesh_shading)
            return {true, "skipped: no mesh shading support"};

        return run_shader_module_test(_device,
                                      LUMINARY_SHADER_STAGE_TASK,
                                      LUMINARY_RHI_SHADER_STAGE_TASK,
                                      "ASMain");
    }

    void cleanup() override {}
};

REGISTER_TEST(shader_module_task_test);

// ---------------------------------------------------------------------------

class shader_module_mesh_test : public test
{
    LRHIDevice _device = nullptr;
public:
    shader_module_mesh_test()
    {
        type        = test_type::validation;
        name        = "shader_module_mesh";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override { _device = device; }

    test_result run(bool /*bake_mode*/) override
    {
        if (!lrhi_get_device_info(_device).features.mesh_shading)
            return {true, "skipped: no mesh shading support"};

        return run_shader_module_test(_device,
                                      LUMINARY_SHADER_STAGE_MESH,
                                      LUMINARY_RHI_SHADER_STAGE_MESH,
                                      "MSMain");
    }

    void cleanup() override {}
};

REGISTER_TEST(shader_module_mesh_test);
