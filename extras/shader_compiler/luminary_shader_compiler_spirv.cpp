#include "luminary_shader_compiler.h"

// SPIR-V compilation path — not yet implemented.
uint8_t* __luminary_compile_shader_spirv(const LuminaryShaderCompilerOptions* /*options*/, uint64_t* out_bytecode_size)
{
    *out_bytecode_size = 0;
    return nullptr;
}
