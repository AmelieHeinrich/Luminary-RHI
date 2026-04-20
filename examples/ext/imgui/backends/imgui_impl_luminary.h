// dear imgui: Renderer Backend for Luminary RHI
// This needs to be used along with a Platform Backend (e.g. OSX)

#pragma once
#include "../imgui.h"
#ifndef IMGUI_DISABLE

#include "luminary_rhi.h"

struct ImGui_ImplLuminary_InitInfo
{
    LRHIDevice device;
    int num_frames_in_flight;
    LRHITextureFormat render_target_format;
    void* user_data;
    LRHIResidencySet residency_set;

    ImGui_ImplLuminary_InitInfo() { memset((void*)this, 0, sizeof(*this)); }
};

struct ImGui_ImplLuminary_RenderState
{
    LRHIRenderPass render_pass;
    LRHIRenderPipeline pipeline;
};

IMGUI_IMPL_API bool ImGui_ImplLuminary_Init(ImGui_ImplLuminary_InitInfo* info);
IMGUI_IMPL_API void ImGui_ImplLuminary_Shutdown();
IMGUI_IMPL_API void ImGui_ImplLuminary_NewFrame();
IMGUI_IMPL_API void ImGui_ImplLuminary_RenderDrawData(ImDrawData* draw_data, LRHIRenderPass render_pass);

IMGUI_IMPL_API bool ImGui_ImplLuminary_CreateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplLuminary_InvalidateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplLuminary_UpdateTexture(ImTextureData* tex);

#endif // #ifndef IMGUI_DISABLE