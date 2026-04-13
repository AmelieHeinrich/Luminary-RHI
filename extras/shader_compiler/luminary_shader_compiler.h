#ifndef LUMINARY_SHADER_COMPILER_H
#define LUMINARY_SHADER_COMPILER_H

#include <stdint.h>

// You are responsible for handling the memory of the returned bytecode, as well as shader include handling.
// The shader compiler has this specific set of rules:
// 1. In Vulkan, the descriptor set layout will have a mutable resource heap at (0, 0),
//    a sampler heap at (1, 0) and a list of TLAS at (2, 0).
//    It will also assume a push constant range of 128 bytes.
//
// 2. In D3D12, the root signature will have the root constants at index 0, and the draw ID constant at index 1.
//
// 3. For Metal 3 and 4, you have the choice between either the default resource heap and sampler heap, + draw ID at index 1,
//    or use MSL and only have access to push constants.
//
// If you wish to use your own, you have to respect those rules.
// Vulkan backend takes SPIRV, D3D12 takes DXIL, and Metal 3/4 takes Metallib from either Metal Shader Converter or raw MSL.
//
// You can find the shader resource utilities in extras/shader_includes/LuminaryRHI.hlsli and
// extras/shader_includes/LuminaryRHI_Metal.h
//

/// The shading language the compiler should target.
typedef enum LuminaryShadingLanguage {
    LUMINARY_SHADING_LANGUAGE_MSL, // Metal Shading Language (supports Metal3/4)
    LUMINARY_SHADING_LANGUAGE_HLSL // High-Level Shading Language (supports every desktop backend)
} LuminaryShadingLanguage;

typedef enum LuminaryShadingBytecode {
    LUMINARY_SHADING_BYTECODE_SPIRV, // Vulkan
    LUMINARY_SHADING_BYTECODE_DXIL, // D3D12
    LUMINARY_SHADING_BYTECODE_METALLIB // Metal3/4
} LuminaryShadingBytecode;

typedef enum LuminaryShaderStage {
    LUMINARY_SHADER_STAGE_VERTEX,
    LUMINARY_SHADER_STAGE_FRAGMENT,
    LUMINARY_SHADER_STAGE_COMPUTE,
    LUMINARY_SHADER_STAGE_MESH,
    LUMINARY_SHADER_STAGE_TASK
} LuminaryShaderStage;

typedef struct LuminaryShaderCompilerOptions {
    LuminaryShadingLanguage shading_language;
    LuminaryShadingBytecode bytecode;
    LuminaryShaderStage shader_stage;

    char entry_point[256];

    char* source_code;
    uint64_t source_code_size;

    const char* defines[256];
    uint32_t defines_count;

    uint8_t use_point_topology; // If using point topology on the Metal Shader Converter path
    uint8_t use_raytracing;
    uint8_t add_debug_symbols;
} LuminaryShaderCompilerOptions;

/// Compiles the shader according to the provided options. Returns a pointer to the compiled bytecode, and writes the size of the bytecode to the output parameter.
/// The caller is responsible for freeing the returned bytecode using free().
uint8_t* luminary_compile_shader(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size);

#endif
