#include "luminary_shader_compiler.h"

// MSL compilation path — not yet implemented.
uint8_t* __luminary_compile_shader_msl(const LuminaryShaderCompilerOptions* /*options*/, uint64_t* out_bytecode_size)
{
    *out_bytecode_size = 0;
    return nullptr;
}
