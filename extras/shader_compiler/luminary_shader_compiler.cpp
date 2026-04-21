#include "luminary_shader_compiler.h"

#if defined(LRHI_MACOS)
extern uint8_t* __luminary_compile_shader_msl(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size);
extern uint8_t* __luminary_compile_shader_msc(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size);
#else
extern uint8_t* __luminary_compile_shader_msl(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size);
extern uint8_t* __luminary_compile_shader_dxil(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size);
extern uint8_t* __luminary_compile_shader_spirv(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size);
extern uint8_t* __luminary_compile_shader_msc(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size);
#endif

uint8_t* luminary_compile_shader(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size)
{
    switch (options->bytecode) {
#if defined(LRHI_MACOS)
        case LUMINARY_SHADING_BYTECODE_METALLIB:
            if (options->shading_language == LUMINARY_SHADING_LANGUAGE_MSL) {
                return __luminary_compile_shader_msl(options, out_bytecode_size);
            } else {
                return __luminary_compile_shader_msc(options, out_bytecode_size);
            }
#elif defined(LRHI_WINDOWS)
        case LUMINARY_SHADING_BYTECODE_DXIL:
            return __luminary_compile_shader_dxil(options, out_bytecode_size);
        case LUMINARY_SHADING_BYTECODE_SPIRV:
            return __luminary_compile_shader_spirv(options, out_bytecode_size);
#else
        case LUMINARY_SHADING_BYTECODE_DXIL:
        case LUMINARY_SHADING_BYTECODE_SPIRV:
            return nullptr;
#endif
    }
    return nullptr;
}
