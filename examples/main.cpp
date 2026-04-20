#include "luminary_rhi.h"
#include "window.h"
#include "examples/compute_particles_example.h"
#include "examples/cornell_pathtracer_example.h"
#include "examples/hello_cube_example.h"
#include "examples/hello_triangle_example.h"
#include "examples/volumetrics_example.h"
#include "imgui/imgui.h"

#include "imgui/backends/imgui_impl_osx.h"
#include "imgui/backends/imgui_impl_luminary.h"

#include <cfloat>
#include <cstdio>
#include <memory>
#include <utility>

static const char* backend_to_string(LRHIBackend backend)
{
    switch (backend) {
        case LUMINARY_RHI_BACKEND_VULKAN: return "Vulkan";
        case LUMINARY_RHI_BACKEND_D3D12: return "D3D12";
        case LUMINARY_RHI_BACKEND_METAL3: return "Metal3";
        case LUMINARY_RHI_BACKEND_METAL4: return "Metal4";
        case LUMINARY_RHI_BACKEND_SWITCH: return "Switch";
        case LUMINARY_RHI_BACKEND_PLAYSTATION: return "PlayStation";
        default: return "Unknown";
    }
}

static void DrawDeviceInfo(const LRHIDeviceInfo& info)
{
    ImGui::TextUnformatted("Device Info");
    ImGui::BulletText("GPU: %s", info.device_name[0] ? info.device_name : "Unknown");
    ImGui::BulletText("Backend: %s", backend_to_string(info.backend));

    ImGui::BulletText("Features: RT %s | Mesh %s | Bindless %s | MDI %s",
        info.features.ray_tracing ? "on" : "off",
        info.features.mesh_shading ? "on" : "off",
        info.features.bindless_resources ? "on" : "off",
        info.features.multi_draw_indirect ? "on" : "off");

    ImGui::BulletText("Limits: 2D %u | 3D %u | Layers %u | Buffer %u MB",
        (unsigned)info.limits.max_texture_dimension_2d,
        (unsigned)info.limits.max_texture_dimension_3d,
        (unsigned)info.limits.max_texture_array_layers,
        (unsigned)(info.limits.max_buffer_size / (1024u * 1024u)));
}

static LRHITextureView make_view(LRHIDevice device, LRHITexture tex,
                                 LRHITextureUsage usage,
                                 LRHITextureDimensions dimensions)
{
    LRHITextureViewInfo info = {};
    info.texture           = tex;
    info.base_mip_level    = 0;
    info.mip_level_count   = LUMINARY_TEXTURE_VIEW_ALL_MIPS;
    info.base_array_layer  = 0;
    info.array_layer_count = LUMINARY_TEXTURE_VIEW_ALL_ARRAY_LAYERS;
    info.format            = LUMINARY_RHI_TEXTURE_FORMAT_UNDEFINED;
    info.usage             = usage;
    info.dimensions        = dimensions;
    LRHITextureView view = nullptr;
    lrhi_create_texture_view(device, &info, &view, nullptr);
    return view;
}

template <typename T, typename... Args>
static void AddExample(const char* id,
                       const char* title,
                       const char* description,
                       std::unique_ptr<Example>& active_example,
                       Args&&... args)
{
    ImGui::PushID(id);

    ImGui::BeginGroup();
    ImGui::TextUnformatted(title);
    if (description && description[0]) {
        ImGui::TextWrapped("%s", description);
    }
    if (ImGui::Button("Launch", ImVec2(-FLT_MIN, 0.0f))) {
        std::unique_ptr<Example> example = std::make_unique<T>(std::forward<Args>(args)...);
        if (example->is_ready()) {
            active_example = std::move(example);
        }
    }
    ImGui::EndGroup();
    ImGui::Spacing();

    ImGui::PopID();
    ImGui::Separator();
}

// ---------------------------------------------------------------------------

int main(void)
{
    LRHIError error = {};

    LRHIDevice device = nullptr;
    lrhi_create_device(LUMINARY_RHI_BACKEND_METAL4, &device, 0, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_device: %s\n", error.message);
        return 1;
    }
    LRHIDeviceInfo device_info = lrhi_get_device_info(device);

    LRHICommandQueue queue = nullptr;
    lrhi_create_command_queue(device, &queue, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_command_queue: %s\n", error.message);
        lrhi_destroy_device(device);
        return 1;
    }

    Window* window = Window::create();
    int width, height;
    window->get_width_and_height(&width, &height);

    LRHISwapChainInfo sc_info = {};
    sc_info.width              = width;
    sc_info.height             = height;
    sc_info.format             = LUMINARY_RHI_TEXTURE_FORMAT_B8G8R8A8_UNORM;
    sc_info.max_frames_in_flight = 3;
    sc_info.handle_type        = LUMINARY_RHI_SWAP_CHAIN_HANDLE_TYPE_METAL_LAYER;
    sc_info.handle.metal_layer = window->get_swap_chain_handle();

    LRHISwapChain swap_chain = nullptr;
    lrhi_create_swap_chain(device, queue, &sc_info, &swap_chain, &error);
    if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        printf("create_swap_chain: %s\n", error.message);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    LRHIFence fence = nullptr;
    uint64_t  fence_val = 0;
    lrhi_create_fence(device, 0, &fence, &error);
    
    LRHIResidencySet rs = nullptr;
    lrhi_create_residency_set(device, &rs, nullptr);
    lrhi_command_queue_add_residency_set(queue, rs, nullptr);

    LRHICommandList cmd = nullptr;
    lrhi_create_command_list(queue, &cmd, &error);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.FontDefault = io.Fonts->AddFontFromFileTTF("extras/examples_assets/DMMono-Regular.ttf", 14.0f);

    ImGui_ImplOSX_Init((__bridge NSView*)window->get_native_view_handle());

    ImGui_ImplLuminary_InitInfo imgui_info = {};
    imgui_info.device = device;
    imgui_info.num_frames_in_flight = sc_info.max_frames_in_flight;
    imgui_info.render_target_format = sc_info.format;
    imgui_info.residency_set = rs;
    if (!ImGui_ImplLuminary_Init(&imgui_info)) {
        printf("ImGui_ImplLuminary_Init failed\n");
        lrhi_destroy_command_list(cmd);
        lrhi_destroy_residency_set(rs);
        lrhi_destroy_fence(fence);
        lrhi_destroy_swap_chain(swap_chain);
        lrhi_destroy_command_queue(queue);
        lrhi_destroy_device(device);
        delete window;
        return 1;
    }

    std::unique_ptr<Example> active_example;

    // ---------------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------------
    while (!window->should_close()) {
#ifdef __APPLE__
        @autoreleasepool {
#else
            {
#endif
            window->poll_events();
            if (window->should_close())
                break;

            window->get_width_and_height(&width, &height);
            if (width <= 0 || height <= 0)
                continue;
            
            // Acquire swapchain texture (borrowed — never destroyed by us)
            LRHITexture sc_tex = lrhi_swap_chain_get_current_texture(swap_chain, &error);
            if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                printf("get_current_texture: %s\n", error.message);
                break;
            }
        
            lrhi_residency_set_update(rs, nullptr);
        
            // Per-frame texture view
            LRHITextureView sc_view = make_view(device, sc_tex,
                LUMINARY_RHI_TEXTURE_USAGE_RENDER_TARGET,
                LUMINARY_RHI_TEXTURE_DIMENSIONS_2D);
            
            lrhi_command_list_reset(cmd, &error);
            lrhi_command_list_begin(cmd, &error);

            if (window->consume_escape_pressed()) {
                active_example.reset();
            }

            ImGui_ImplOSX_NewFrame((__bridge NSView*)window->get_native_view_handle());
            ImGui_ImplLuminary_NewFrame();
            ImGui::NewFrame();

            if (!active_example) {
                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->WorkPos);
                ImGui::SetNextWindowSize(viewport->WorkSize);
                ImGui::SetNextWindowViewport(viewport->ID);

                ImGuiWindowFlags menu_flags = ImGuiWindowFlags_NoDecoration |
                                              ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus |
                                              ImGuiWindowFlags_NoNavFocus;

                ImGui::Begin("Luminary Examples", nullptr, menu_flags);
                ImGui::TextUnformatted("Select an example to run.");
                ImGui::Spacing();
                DrawDeviceInfo(device_info);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                AddExample<HelloTriangleExample>(
                    "hello_triangle",
                    "Hello Triangle",
                    "A minimal graphics example that renders a single triangle.",
                    active_example,
                    device,
                    sc_info.format);
                AddExample<HelloCubeExample>(
                    "hello_cube",
                    "Hello Cube",
                    "A spinning textured cube with a CPU-generated rainbow checkerboard.",
                    active_example,
                    device,
                    sc_info.format);
                AddExample<ComputeParticlesExample>(
                    "compute_particles",
                    "Compute Particles",
                    "A 1,000,000-particle galaxy updated in compute and rendered as points.",
                    active_example,
                    device,
                    sc_info.format);
                AddExample<VolumetricsExample>(
                    "volumetrics",
                    "Volumetrics",
                    "Simple volumetric clouds using a compute-generated Worley 3D texture and raymarching.",
                    active_example,
                    device,
                    sc_info.format);
                AddExample<CornellPathtracerExample>(
                    "cornell_pathtracer",
                    "Cornell Pathtracer",
                    "CPU-built Cornell box traced in compute RayQuery with history accumulation and tonemapping.",
                    active_example,
                    device,
                    sc_info.format);

                ImGui::End();
            }

            if (active_example) {
                active_example->record(cmd, sc_view, width, height, rs);
                active_example->draw_ui();
            }

            ImGui::Render();

            // ImGui overlay pass
            LRHIRenderPassInfo rp_info = {};
            rp_info.color_attachments[0].texture_view = sc_view;
            rp_info.color_attachments[0].load_action = active_example
                ? LUMINARY_RHI_RENDER_PASS_ACTION_LOAD
                : LUMINARY_RHI_RENDER_PASS_ACTION_CLEAR;
            rp_info.color_attachments[0].store_action = LUMINARY_RHI_RENDER_PASS_ACTION_STORE;
            rp_info.color_attachments[0].clear_color[0] = 0.1f;
            rp_info.color_attachments[0].clear_color[1] = 0.1f;
            rp_info.color_attachments[0].clear_color[2] = 0.1f;
            rp_info.color_attachments[0].clear_color[3] = 1.0f;
            rp_info.color_attachment_count              = 1;
            rp_info.render_width                        = (uint32_t)width;
            rp_info.render_height                       = (uint32_t)height;

            LRHIRenderPass rp = lrhi_render_pass_begin(cmd, &rp_info, &error);

            if (error.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !rp) {
                lrhi_destroy_texture_view(sc_view);
                break;
            }

            if (active_example) {
                lrhi_render_pass_encoder_barrier(
                    rp,
                    (LRHIRenderStage)(LUMINARY_RHI_RENDER_STAGE_VERTEX |
                                      LUMINARY_RHI_RENDER_STAGE_FRAGMENT |
                                      LUMINARY_RHI_RENDER_STAGE_COMPUTE |
                                      LUMINARY_RHI_RENDER_STAGE_MESH |
                                      LUMINARY_RHI_RENDER_STAGE_TASK |
                                      LUMINARY_RHI_RENDER_STAGE_ACCELERATION_STRUCTURE_BUILD |
                                      LUMINARY_RHI_RENDER_STAGE_COPY),
                    LUMINARY_RHI_RENDER_STAGE_FRAGMENT,
                    &error);
            }

            ImGui_ImplLuminary_RenderDrawData(ImGui::GetDrawData(), rp);

            lrhi_render_pass_end(rp, &error);
            lrhi_command_list_end(cmd, &error);
            
            // Submit and wait
            ++fence_val;
            lrhi_command_queue_submit(queue, &cmd, 1, fence, fence_val, nullptr, 0, nullptr);
            lrhi_fence_wait(fence, fence_val, 5000000000ULL, nullptr);
            
            lrhi_swap_chain_present(swap_chain, &error);
            
            // Per-frame cleanup
            lrhi_destroy_texture_view(sc_view);
        }
    }

    // ---------------------------------------------------------------------------
    // Cleanup
    // ---------------------------------------------------------------------------
    active_example.reset();
    ImGui_ImplLuminary_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();

    lrhi_destroy_command_list(cmd);
    lrhi_destroy_residency_set(rs);
    lrhi_destroy_fence(fence);
    lrhi_destroy_swap_chain(swap_chain);
    lrhi_destroy_command_queue(queue);
    lrhi_destroy_device(device);
    delete window;
    return 0;
}
