#include "luminary_shader_compiler.h"

#include "msc/ir_input_topology.h"
#include "msc/metal_irconverter.h"
#include "msc/WinAdapter.h"
#include "msc/dxcapi.h"

#include <stdio.h>
#include <dlfcn.h>

template<typename T>
class DxcPtr {
    T* m_ptr = nullptr;
public:
    DxcPtr() = default;
    DxcPtr(const DxcPtr&) = delete;
    DxcPtr& operator=(const DxcPtr&) = delete;
    ~DxcPtr() { if (m_ptr) m_ptr->Release(); }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }
    T* Get() const { return m_ptr; }
    T** GetAddressOf() { return &m_ptr; }
};

inline const char* ProfileFromType(LuminaryShaderStage type)
{
    switch (type)
    {
    case LuminaryShaderStage::LUMINARY_SHADER_STAGE_VERTEX:
        return "vs_6_6";
    case LuminaryShaderStage::LUMINARY_SHADER_STAGE_FRAGMENT:
        return "ps_6_6";
    case LuminaryShaderStage::LUMINARY_SHADER_STAGE_COMPUTE:
        return "cs_6_6";
    case LuminaryShaderStage::LUMINARY_SHADER_STAGE_MESH:
        return "ms_6_6";
    case LuminaryShaderStage::LUMINARY_SHADER_STAGE_TASK:
        return "as_6_6";
    default:
        return "";
    }
    return "";
}

uint8_t* __luminary_compile_shader_msc(const LuminaryShaderCompilerOptions* options, uint64_t* out_bytecode_size)
{
    // Create root signature
    IRRootParameter1 root_parameters[2] = {};
    root_parameters[0] = {
        .ParameterType = IRRootParameterType32BitConstants,
        .Constants = {
            .ShaderRegister = 0,
            .RegisterSpace = 0,
            .Num32BitValues = 128 / sizeof(uint32_t)
        },
        .ShaderVisibility = IRShaderVisibilityAll
    };
    root_parameters[1] = {
        .ParameterType = IRRootParameterType32BitConstants,
        .Constants = {
            .ShaderRegister = 1,
            .RegisterSpace = 0,
            .Num32BitValues = 1
        },
        .ShaderVisibility = IRShaderVisibilityAll
    };

    IRVersionedRootSignatureDescriptor root_sig_descriptor = {};
    root_sig_descriptor.version = IRRootSignatureVersion_1_1;
    root_sig_descriptor.desc_1_1.Flags = IRRootSignatureFlags(IRRootSignatureFlagSamplerHeapDirectlyIndexed | IRRootSignatureFlagCBVSRVUAVHeapDirectlyIndexed);
    root_sig_descriptor.desc_1_1.pParameters = root_parameters;
    root_sig_descriptor.desc_1_1.NumParameters = 2;

    IRError* root_sig_error = nullptr;
    IRRootSignature* root_sig = IRRootSignatureCreateFromDescriptor(&root_sig_descriptor, &root_sig_error);
    if (root_sig_error) {
        auto errorCode = IRErrorGetCode(root_sig_error);

        fprintf(stderr, "Failed to create root signature: %d\n", errorCode);
        IRErrorDestroy(root_sig_error);
        return nullptr;
    }

    // Compile with DXC, load DxcCreateInstance from dylib
    void* dxcLib = dlopen("libdxcompiler.dylib", RTLD_NOW);
    if (!dxcLib) {
        fprintf(stderr, "Failed to load libdxcompiler.dylib: %s\n", dlerror());
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }

    auto createInstance = (HRESULT(*)(REFCLSID, REFIID, void**))dlsym(dxcLib, "DxcCreateInstance");
    if (!createInstance) {
        fprintf(stderr, "Failed to find DxcCreateInstance: %s\n", dlerror());
        dlclose(dxcLib);
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }

    DxcPtr<IDxcUtils> pUtils;
    DxcPtr<IDxcCompiler3> pCompiler;
    DxcPtr<IDxcIncludeHandler> pIncludeHandler;
    DxcPtr<IDxcResult> pResult;
    DxcPtr<IDxcBlob> pShaderBlob;
    DxcPtr<IDxcBlobUtf8> pErrorsU8;

    wchar_t wideTarget[512] = {0};
    swprintf_s(wideTarget, 512, L"%hs", ProfileFromType(options->shader_stage));

    wchar_t wideEntry[512] = {0};
    swprintf_s(wideEntry, 512, L"%hs", options->entry_point);

    if (FAILED(createInstance(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf())))) {
        fprintf(stderr, "Failed to create DXC utils!");
        dlclose(dxcLib);
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }
    if (FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf())))) {
        fprintf(stderr, "Failed to create DXC compiler!");
        dlclose(dxcLib);
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }
    if (FAILED(pUtils->CreateDefaultIncludeHandler(pIncludeHandler.GetAddressOf()))) {
        fprintf(stderr, "Failed to create include handler!");
        dlclose(dxcLib);
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }

    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = options->source_code;
    sourceBuffer.Size = options->source_code_size;
    sourceBuffer.Encoding = DXC_CP_UTF8;

    std::vector<std::wstring> wideDefines;
    std::vector<LPCWSTR> compileArgs = {};
    compileArgs.push_back(L"-E");
    compileArgs.push_back(wideEntry);
    compileArgs.push_back(L"-T");
    compileArgs.push_back(wideTarget);
    if (options->add_debug_symbols) {
        compileArgs.push_back(L"-Qembed_debug");
        compileArgs.push_back(L"-Zi");
    }
    if (options->use_raytracing) {
        compileArgs.push_back(L"-DLUMINARY_RAYTRACING");
    }
    compileArgs.push_back(L"-DLUMINARY_METAL");

    for (int i = 0; i < options->defines_count; i++) {
        wideDefines.push_back(L"-D" + std::wstring(options->defines[i], options->defines[i] + strlen(options->defines[i])));
        compileArgs.push_back(wideDefines.back().c_str());
    }

    HRESULT hresult = pCompiler->Compile(&sourceBuffer, compileArgs.data(), (uint32_t)compileArgs.size(), pIncludeHandler.Get(), IID_PPV_ARGS(pResult.GetAddressOf()));
    if (FAILED(hresult)) {
        fprintf(stderr, "Failed to compile shader: 0x%08X\n", hresult);
        dlclose(dxcLib);
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }

    if (SUCCEEDED(pResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrorsU8.GetAddressOf()), nullptr)) && pErrorsU8 && pErrorsU8->GetStringLength() > 0) {
        fprintf(stderr, "Shader compilation warnings/errors:\n%s\n", pErrorsU8->GetStringPointer());
    }

    HRESULT status = S_OK;
    if (FAILED(pResult->GetStatus(&status)) || FAILED(status)) {
        fprintf(stderr, "Shader compilation failed with status: 0x%08X\n", status);
        dlclose(dxcLib);
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }

    // Get result blob
    if (FAILED(pResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(pShaderBlob.GetAddressOf()), nullptr)) || !pShaderBlob) {
        fprintf(stderr, "Failed to get compiled shader blob!\n");
        dlclose(dxcLib);
        IRRootSignatureDestroy(root_sig);
        return nullptr;
    }

    // Now do MSC
    auto module = IRObjectCreateFromDXIL((const uint8_t*)pShaderBlob->GetBufferPointer(), (size_t)pShaderBlob->GetBufferSize(), IRBytecodeOwnershipNone);

    IRCompiler* compiler = IRCompilerCreate();
    IRCompilerSetEntryPointName(compiler, options->entry_point);
    IRCompilerSetMinimumDeploymentTarget(compiler, IROperatingSystem_macOS, "16.0");
    IRCompilerSetGlobalRootSignature(compiler, root_sig);
    if (options->use_point_topology) {
        IRCompilerSetInputTopology(compiler, IRInputTopologyPoint);
    }
    IRCompilerSetValidationFlags(compiler, IRCompilerValidationFlagValidateDXIL);
    IRCompilerSetStageInGenerationMode(compiler, IRStageInCodeGenerationModeUseSeparateStageInFunction);

    IRError* compileError = nullptr;
    auto metalIR = IRCompilerAllocCompileAndLink(compiler, options->entry_point, module, &compileError);
    if (compileError) {
        auto errorCode = IRErrorGetCode(compileError);

        fprintf(stderr, "Metal IR generation failed with code %u\n", errorCode);
        IRErrorDestroy(compileError);
    }
    if (!metalIR) {
        fprintf(stderr, "IRCompilerAllocCompileAndLink returned null\n");
        IRObjectDestroy(module);
        IRCompilerDestroy(compiler);
        IRRootSignatureDestroy(root_sig);
        dlclose(dxcLib);
        return nullptr;
    }

    IRMetalLibBinary* pMetallib = IRMetalLibBinaryCreate();
    if (!IRObjectGetMetalLibBinary(metalIR, IRObjectGetMetalIRShaderStage(metalIR), pMetallib)) {
        fprintf(stderr, "Failed to get Metal lib binary from compiled shader\n");
        IRMetalLibBinaryDestroy(pMetallib);
        IRObjectDestroy(module);
        IRObjectDestroy(metalIR);
        IRCompilerDestroy(compiler);
        IRRootSignatureDestroy(root_sig);
        dlclose(dxcLib);
        return nullptr;
    }
    uint64_t bytecodeSize = IRMetalLibGetBytecodeSize(pMetallib);

    uint8_t* result = (uint8_t*)malloc(bytecodeSize);
    if (!result) {
        fprintf(stderr, "Failed to allocate memory for shader bytecode\n");
        IRMetalLibBinaryDestroy(pMetallib);
        IRObjectDestroy(module);
        IRObjectDestroy(metalIR);
        IRCompilerDestroy(compiler);
        IRRootSignatureDestroy(root_sig);
        dlclose(dxcLib);
        return nullptr;
    }
    IRMetalLibGetBytecode(pMetallib, result);
    *out_bytecode_size = bytecodeSize;

    IRMetalLibBinaryDestroy(pMetallib);
    IRObjectDestroy(module);
    IRObjectDestroy(metalIR);
    IRCompilerDestroy(compiler);
    IRRootSignatureDestroy(root_sig);
    dlclose(dxcLib);
    return result;
}
