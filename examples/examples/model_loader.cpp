#include "model_loader.h"

#include "../ext/cgltf/cgltf.h"
#include "../ext/stb/stb_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct CpuVertex
{
    float position[4];
    float normal[4];
    float tangent[4];
    float uv0[4];
    float color0[4];
};

struct CpuSkinnedVertex
{
    float position[4];
    float normal[4];
    float tangent[4];
    float uv0[4];
    float color0[4];
    uint32_t joints[4];
    float weights[4];
};

struct LogicalTextureKey
{
    const cgltf_texture* texture;
    bool srgb;
};

struct LogicalTextureKeyHash
{
    size_t operator()(const LogicalTextureKey& key) const
    {
        size_t a = reinterpret_cast<size_t>(key.texture);
        return (a >> 4u) ^ static_cast<size_t>(key.srgb ? 0x9e3779b9u : 0x85ebca6bu);
    }
};

struct LogicalTextureKeyEq
{
    bool operator()(const LogicalTextureKey& a, const LogicalTextureKey& b) const
    {
        return a.texture == b.texture && a.srgb == b.srgb;
    }
};

static MaterialTextureSlot make_empty_slot()
{
    MaterialTextureSlot out = {};
    out.texture_index = MODEL_LOADER_INVALID_INDEX;
    out.texcoord_set = 0;
    out.scale = 1.0f;
    out.has_transform = 0;
    out.offset[0] = 0.0f;
    out.offset[1] = 0.0f;
    out.uv_scale[0] = 1.0f;
    out.uv_scale[1] = 1.0f;
    out.rotation_radians = 0.0f;
    return out;
}

static std::string safe_name(const char* name, const char* fallback)
{
    if (name && name[0]) {
        return std::string(name);
    }
    return std::string(fallback);
}

static void copy16(const float* src, float* dst)
{
    for (int i = 0; i < 16; ++i) {
        dst[i] = src[i];
    }
}

static void normalize3(float v[3])
{
    float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (len2 <= 1e-20f) {
        v[0] = 0.0f;
        v[1] = 1.0f;
        v[2] = 0.0f;
        return;
    }
    float inv = 1.0f / std::sqrt(len2);
    v[0] *= inv;
    v[1] *= inv;
    v[2] *= inv;
}

static void transform_point(const float m[16], const float in[3], float out[3])
{
    out[0] = m[0] * in[0] + m[4] * in[1] + m[8] * in[2] + m[12];
    out[1] = m[1] * in[0] + m[5] * in[1] + m[9] * in[2] + m[13];
    out[2] = m[2] * in[0] + m[6] * in[1] + m[10] * in[2] + m[14];
}

static bool invert_3x3_column_major(const float m[16], float out[9])
{
    float a00 = m[0], a01 = m[4], a02 = m[8];
    float a10 = m[1], a11 = m[5], a12 = m[9];
    float a20 = m[2], a21 = m[6], a22 = m[10];

    float b01 = a22 * a11 - a12 * a21;
    float b11 = -a22 * a10 + a12 * a20;
    float b21 = a21 * a10 - a11 * a20;

    float det = a00 * b01 + a01 * b11 + a02 * b21;
    if (std::abs(det) <= 1e-20f) {
        return false;
    }
    float inv_det = 1.0f / det;

    out[0] = b01 * inv_det;
    out[1] = (-a22 * a01 + a02 * a21) * inv_det;
    out[2] = (a12 * a01 - a02 * a11) * inv_det;
    out[3] = b11 * inv_det;
    out[4] = (a22 * a00 - a02 * a20) * inv_det;
    out[5] = (-a12 * a00 + a02 * a10) * inv_det;
    out[6] = b21 * inv_det;
    out[7] = (-a21 * a00 + a01 * a20) * inv_det;
    out[8] = (a11 * a00 - a01 * a10) * inv_det;
    return true;
}

static void transform_direction_with_normal_matrix(const float world[16], const float in[3], float out[3])
{
    float inv3[9] = {};
    if (!invert_3x3_column_major(world, inv3)) {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        normalize3(out);
        return;
    }

    out[0] = inv3[0] * in[0] + inv3[1] * in[1] + inv3[2] * in[2];
    out[1] = inv3[3] * in[0] + inv3[4] * in[1] + inv3[5] * in[2];
    out[2] = inv3[6] * in[0] + inv3[7] * in[1] + inv3[8] * in[2];
    normalize3(out);
}

static AnimationInterpolation map_interpolation(cgltf_interpolation_type interpolation)
{
    switch (interpolation) {
        case cgltf_interpolation_type_step:
            return AnimationInterpolation::Step;
        case cgltf_interpolation_type_cubic_spline:
            return AnimationInterpolation::CubicSpline;
        case cgltf_interpolation_type_linear:
        default:
            return AnimationInterpolation::Linear;
    }
}

static AnimationTargetPath map_target_path(cgltf_animation_path_type path)
{
    switch (path) {
        case cgltf_animation_path_type_rotation:
            return AnimationTargetPath::Rotation;
        case cgltf_animation_path_type_scale:
            return AnimationTargetPath::Scale;
        case cgltf_animation_path_type_weights:
            return AnimationTargetPath::Weights;
        case cgltf_animation_path_type_translation:
        default:
            return AnimationTargetPath::Translation;
    }
}

template <typename T>
static uint32_t append_buffer_job(Mesh_Result& result,
                                  MeshBufferKind kind,
                                  LRHIBufferUsage usage,
                                  const std::string& debug_name,
                                  const std::vector<T>& payload)
{
    BufferUploadJob job = {};
    job.kind = kind;
    job.usage = usage;
    job.stride = sizeof(T);
    job.debug_name = debug_name;
    job.bytes.resize(payload.size() * sizeof(T));
    if (!payload.empty()) {
        memcpy(job.bytes.data(), payload.data(), job.bytes.size());
    }

    result.buffer_upload_jobs.push_back(std::move(job));
    return static_cast<uint32_t>(result.buffer_upload_jobs.size() - 1);
}

static uint32_t append_index_buffer_job_u32(Mesh_Result& result,
                                            const std::string& debug_name,
                                            const std::vector<uint32_t>& indices)
{
    BufferUploadJob job = {};
    job.kind = MeshBufferKind::Index;
    job.usage = (LRHIBufferUsage)(LUMINARY_RHI_BUFFER_USAGE_INDEX | LUMINARY_RHI_BUFFER_USAGE_SHADER_READ);
    job.stride = sizeof(uint32_t);
    job.debug_name = debug_name;
    job.bytes.resize(indices.size() * sizeof(uint32_t));
    if (!indices.empty()) {
        memcpy(job.bytes.data(), indices.data(), job.bytes.size());
    }

    result.buffer_upload_jobs.push_back(std::move(job));
    return static_cast<uint32_t>(result.buffer_upload_jobs.size() - 1);
}

static uint32_t append_buffer_job_raw(Mesh_Result& result,
                                      MeshBufferKind kind,
                                      LRHIBufferUsage usage,
                                      uint64_t stride,
                                      const std::string& debug_name,
                                      const void* data,
                                      size_t size_bytes)
{
    BufferUploadJob job = {};
    job.kind = kind;
    job.usage = usage;
    job.stride = stride;
    job.debug_name = debug_name;
    job.bytes.resize(size_bytes);
    if (size_bytes > 0 && data != nullptr) {
        memcpy(job.bytes.data(), data, size_bytes);
    }

    result.buffer_upload_jobs.push_back(std::move(job));
    return static_cast<uint32_t>(result.buffer_upload_jobs.size() - 1);
}

static const cgltf_accessor* find_attr(const cgltf_primitive* primitive, cgltf_attribute_type type, int index)
{
    return cgltf_find_accessor(primitive, type, index);
}

static bool read_floats(const cgltf_accessor* accessor,
                        size_t element_index,
                        float* out,
                        size_t element_size)
{
    if (!accessor) {
        return false;
    }
    return cgltf_accessor_read_float(accessor, element_index, out, element_size) != 0;
}

static bool decode_image_rgba8(const cgltf_image* image,
                               const std::filesystem::path& model_path,
                               std::vector<uint8_t>& out_pixels,
                               uint32_t& out_w,
                               uint32_t& out_h,
                               std::string& out_error)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = nullptr;

    if (image->buffer_view != nullptr) {
        const uint8_t* bytes = cgltf_buffer_view_data(image->buffer_view);
        if (bytes == nullptr || image->buffer_view->size == 0) {
            out_error = "image buffer view is empty";
            return false;
        }
        pixels = stbi_load_from_memory(bytes, static_cast<int>(image->buffer_view->size), &width, &height, &channels, 4);
    } else if (image->uri && image->uri[0]) {
        std::string uri = image->uri;
        if (uri.rfind("data:", 0) == 0) {
            out_error = "data URI textures are not supported yet";
            return false;
        }

        std::filesystem::path resolved = model_path.parent_path() / std::filesystem::path(uri);
        pixels = stbi_load(resolved.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            out_error = "failed to load image file: " + resolved.string();
            return false;
        }
    } else {
        out_error = "image has neither uri nor buffer_view";
        return false;
    }

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) {
            stbi_image_free(pixels);
        }
        out_error = "decoded image is invalid";
        return false;
    }

    out_w = static_cast<uint32_t>(width);
    out_h = static_cast<uint32_t>(height);
    out_pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    memcpy(out_pixels.data(), pixels, out_pixels.size());
    stbi_image_free(pixels);
    return true;
}

static MaterialTextureSlot build_material_texture_slot(const cgltf_texture_view& view,
                                                       uint32_t texture_index)
{
    MaterialTextureSlot slot = make_empty_slot();
    slot.texture_index = texture_index;
    slot.texcoord_set = view.texcoord;
    slot.scale = view.scale;
    if (slot.scale == 0.0f) {
        slot.scale = 1.0f;
    }

    if (view.has_transform) {
        slot.has_transform = 1;
        slot.offset[0] = view.transform.offset[0];
        slot.offset[1] = view.transform.offset[1];
        slot.uv_scale[0] = view.transform.scale[0] == 0.0f ? 1.0f : view.transform.scale[0];
        slot.uv_scale[1] = view.transform.scale[1] == 0.0f ? 1.0f : view.transform.scale[1];
        slot.rotation_radians = view.transform.rotation;
        if (view.transform.has_texcoord) {
            slot.texcoord_set = view.transform.texcoord;
        }
    }

    return slot;
}

static uint32_t ensure_texture(const cgltf_texture_view& view,
                               bool srgb,
                               const std::filesystem::path& model_path,
                               Mesh_Result& result,
                               std::unordered_map<LogicalTextureKey, uint32_t, LogicalTextureKeyHash, LogicalTextureKeyEq>& texture_cache)
{
    if (!view.texture || !view.texture->image) {
        return MODEL_LOADER_INVALID_INDEX;
    }

    LogicalTextureKey key = {view.texture, srgb};
    auto it = texture_cache.find(key);
    if (it != texture_cache.end()) {
        return it->second;
    }

    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string decode_error;

    if (!decode_image_rgba8(view.texture->image, model_path, pixels, width, height, decode_error)) {
        result.warnings.push_back("Texture decode failed for " + safe_name(view.texture->name, "unnamed_texture") + ": " + decode_error);
        return MODEL_LOADER_INVALID_INDEX;
    }

    TextureUploadJob job = {};
    job.debug_name = safe_name(view.texture->name, "Texture") + (srgb ? " (sRGB)" : " (Linear)");
    job.width = width;
    job.height = height;
    job.format = srgb ? LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_SRGB : LUMINARY_RHI_TEXTURE_FORMAT_R8G8B8A8_UNORM;
    job.bytes_per_row = width * 4u;
    job.bytes_per_image = width * height * 4u;
    job.pixels_rgba8 = std::move(pixels);

    result.texture_upload_jobs.push_back(std::move(job));
    uint32_t upload_index = static_cast<uint32_t>(result.texture_upload_jobs.size() - 1);

    ModelTexture model_texture = {};
    model_texture.name = safe_name(view.texture->name, "Texture");
    model_texture.upload_job_index = upload_index;
    model_texture.srgb = srgb ? 1u : 0u;

    result.model_textures.push_back(std::move(model_texture));
    uint32_t texture_index = static_cast<uint32_t>(result.model_textures.size() - 1);
    texture_cache.emplace(key, texture_index);
    return texture_index;
}

static uint32_t ensure_material(const cgltf_material* material,
                                const std::filesystem::path& model_path,
                                Mesh_Result& result,
                                std::unordered_map<const cgltf_material*, uint32_t>& material_cache,
                                std::unordered_map<LogicalTextureKey, uint32_t, LogicalTextureKeyHash, LogicalTextureKeyEq>& texture_cache)
{
    auto found = material_cache.find(material);
    if (found != material_cache.end()) {
        return found->second;
    }

    ModelMaterial out = {};
    out.name = material ? safe_name(material->name, "Material") : std::string("Default Material");
    out.base_color_factor[0] = 1.0f;
    out.base_color_factor[1] = 1.0f;
    out.base_color_factor[2] = 1.0f;
    out.base_color_factor[3] = 1.0f;
    out.emissive_factor[0] = 0.0f;
    out.emissive_factor[1] = 0.0f;
    out.emissive_factor[2] = 0.0f;
    out.metallic_factor = 1.0f;
    out.roughness_factor = 1.0f;
    out.alpha_cutoff = 0.5f;
    out.alpha_mode = 0;
    out.double_sided = 0;
    out.unlit = 0;

    out.base_color = make_empty_slot();
    out.normal = make_empty_slot();
    out.metallic_roughness = make_empty_slot();
    out.occlusion = make_empty_slot();
    out.emissive = make_empty_slot();

    if (material != nullptr) {
        out.alpha_mode = static_cast<uint8_t>(material->alpha_mode);
        out.alpha_cutoff = material->alpha_cutoff;
        out.double_sided = material->double_sided ? 1u : 0u;
        out.unlit = material->unlit ? 1u : 0u;

        out.emissive_factor[0] = material->emissive_factor[0];
        out.emissive_factor[1] = material->emissive_factor[1];
        out.emissive_factor[2] = material->emissive_factor[2];

        if (material->has_pbr_metallic_roughness) {
            const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
            out.base_color_factor[0] = pbr.base_color_factor[0];
            out.base_color_factor[1] = pbr.base_color_factor[1];
            out.base_color_factor[2] = pbr.base_color_factor[2];
            out.base_color_factor[3] = pbr.base_color_factor[3];
            out.metallic_factor = pbr.metallic_factor;
            out.roughness_factor = pbr.roughness_factor;

            uint32_t base_tex = ensure_texture(pbr.base_color_texture, true, model_path, result, texture_cache);
            if (base_tex != MODEL_LOADER_INVALID_INDEX) {
                out.base_color = build_material_texture_slot(pbr.base_color_texture, base_tex);
            }

            uint32_t mr_tex = ensure_texture(pbr.metallic_roughness_texture, false, model_path, result, texture_cache);
            if (mr_tex != MODEL_LOADER_INVALID_INDEX) {
                out.metallic_roughness = build_material_texture_slot(pbr.metallic_roughness_texture, mr_tex);
            }
        }

        uint32_t normal_tex = ensure_texture(material->normal_texture, false, model_path, result, texture_cache);
        if (normal_tex != MODEL_LOADER_INVALID_INDEX) {
            out.normal = build_material_texture_slot(material->normal_texture, normal_tex);
        }

        uint32_t occ_tex = ensure_texture(material->occlusion_texture, false, model_path, result, texture_cache);
        if (occ_tex != MODEL_LOADER_INVALID_INDEX) {
            out.occlusion = build_material_texture_slot(material->occlusion_texture, occ_tex);
        }

        uint32_t emis_tex = ensure_texture(material->emissive_texture, true, model_path, result, texture_cache);
        if (emis_tex != MODEL_LOADER_INVALID_INDEX) {
            out.emissive = build_material_texture_slot(material->emissive_texture, emis_tex);
        }
    }

    result.model_materials.push_back(std::move(out));
    uint32_t material_index = static_cast<uint32_t>(result.model_materials.size() - 1);
    material_cache.emplace(material, material_index);
    return material_index;
}

static bool read_joint_indices(const cgltf_accessor* accessor, size_t vertex_index, uint32_t out_joints[4])
{
    if (!accessor) {
        return false;
    }

    cgltf_uint values[4] = {};
    if (!cgltf_accessor_read_uint(accessor, vertex_index, values, 4)) {
        return false;
    }

    out_joints[0] = static_cast<uint32_t>(values[0]);
    out_joints[1] = static_cast<uint32_t>(values[1]);
    out_joints[2] = static_cast<uint32_t>(values[2]);
    out_joints[3] = static_cast<uint32_t>(values[3]);
    return true;
}

static void fill_weights_or_default(const cgltf_accessor* accessor, size_t vertex_index, float out_weights[4])
{
    if (!accessor || !cgltf_accessor_read_float(accessor, vertex_index, out_weights, 4)) {
        out_weights[0] = 1.0f;
        out_weights[1] = 0.0f;
        out_weights[2] = 0.0f;
        out_weights[3] = 0.0f;
        return;
    }

    float sum = out_weights[0] + out_weights[1] + out_weights[2] + out_weights[3];
    if (sum <= 1e-20f) {
        out_weights[0] = 1.0f;
        out_weights[1] = 0.0f;
        out_weights[2] = 0.0f;
        out_weights[3] = 0.0f;
        return;
    }

    float inv = 1.0f / sum;
    out_weights[0] *= inv;
    out_weights[1] *= inv;
    out_weights[2] *= inv;
    out_weights[3] *= inv;
}

static void extract_animations(const cgltf_data* data,
                               const std::unordered_map<const cgltf_node*, uint32_t>& node_to_index,
                               Mesh_Result& result)
{
    for (cgltf_size i = 0; i < data->animations_count; ++i) {
        const cgltf_animation* animation = &data->animations[i];
        AnimationClip clip = {};
        clip.name = safe_name(animation->name, "Animation");
        clip.start_time = 0.0f;
        clip.end_time = 0.0f;

        bool has_time = false;

        clip.samplers.reserve(animation->samplers_count);
        for (cgltf_size s = 0; s < animation->samplers_count; ++s) {
            const cgltf_animation_sampler* src = &animation->samplers[s];
            AnimationSampler dst = {};
            dst.interpolation = map_interpolation(src->interpolation);

            if (src->input && src->input->count > 0) {
                dst.input_times.resize(src->input->count);
                for (cgltf_size t = 0; t < src->input->count; ++t) {
                    float v = 0.0f;
                    cgltf_accessor_read_float(src->input, t, &v, 1);
                    dst.input_times[t] = v;
                    if (!has_time) {
                        clip.start_time = v;
                        clip.end_time = v;
                        has_time = true;
                    } else {
                        clip.start_time = std::min(clip.start_time, v);
                        clip.end_time = std::max(clip.end_time, v);
                    }
                }
            }

            if (src->output && src->output->count > 0) {
                uint32_t components = static_cast<uint32_t>(cgltf_num_components(src->output->type));
                dst.output_components = components;
                dst.output_values.resize(src->output->count * components);
                for (cgltf_size k = 0; k < src->output->count; ++k) {
                    cgltf_accessor_read_float(src->output,
                                              k,
                                              &dst.output_values[k * components],
                                              components);
                }
            }

            clip.samplers.push_back(std::move(dst));
        }

        clip.channels.reserve(animation->channels_count);
        for (cgltf_size c = 0; c < animation->channels_count; ++c) {
            const cgltf_animation_channel* channel = &animation->channels[c];
            AnimationChannel out = {};
            out.sampler_index = static_cast<uint32_t>(cgltf_animation_sampler_index(animation, channel->sampler));
            out.target_path = map_target_path(channel->target_path);

            auto node_it = node_to_index.find(channel->target_node);
            out.target_node_index = node_it != node_to_index.end() ? node_it->second : MODEL_LOADER_INVALID_INDEX;

            clip.channels.push_back(out);
        }

        result.optional_animations.push_back(std::optional<AnimationClip>(std::move(clip)));
    }
}

static void extract_skins(const cgltf_data* data,
                          const std::unordered_map<const cgltf_node*, uint32_t>& node_to_index,
                          Mesh_Result& result)
{
    result.model_skins.reserve(data->skins_count);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        const cgltf_skin* skin = &data->skins[i];
        ModelSkin out = {};
        out.name = safe_name(skin->name, "Skin");

        auto root_it = node_to_index.find(skin->skeleton);
        out.skeleton_root_node_index = root_it != node_to_index.end() ? root_it->second : MODEL_LOADER_INVALID_INDEX;

        out.joint_node_indices.reserve(skin->joints_count);
        for (cgltf_size j = 0; j < skin->joints_count; ++j) {
            auto joint_it = node_to_index.find(skin->joints[j]);
            out.joint_node_indices.push_back(joint_it != node_to_index.end() ? joint_it->second : MODEL_LOADER_INVALID_INDEX);
        }

        out.inverse_bind_matrices.resize(skin->joints_count * 16u, 0.0f);
        if (skin->inverse_bind_matrices) {
            for (cgltf_size j = 0; j < skin->joints_count; ++j) {
                cgltf_accessor_read_float(skin->inverse_bind_matrices, j, &out.inverse_bind_matrices[j * 16u], 16u);
            }
        } else {
            for (cgltf_size j = 0; j < skin->joints_count; ++j) {
                out.inverse_bind_matrices[j * 16u + 0u] = 1.0f;
                out.inverse_bind_matrices[j * 16u + 5u] = 1.0f;
                out.inverse_bind_matrices[j * 16u + 10u] = 1.0f;
                out.inverse_bind_matrices[j * 16u + 15u] = 1.0f;
            }
        }

        result.model_skins.push_back(std::move(out));
    }
}

static void extract_node_primitives(const cgltf_node* node,
                                    uint32_t instance_index,
                                    bool skinned_path,
                                    const std::unordered_map<const cgltf_skin*, uint32_t>& skin_to_index,
                                    const std::filesystem::path& model_path,
                                    std::unordered_map<const cgltf_material*, uint32_t>& material_cache,
                                    std::unordered_map<LogicalTextureKey, uint32_t, LogicalTextureKeyHash, LogicalTextureKeyEq>& texture_cache,
                                    Mesh_Result& result)
{
    if (!node->mesh) {
        return;
    }

    float world[16] = {};
    cgltf_node_transform_world(node, world);

    uint32_t skin_index = MODEL_LOADER_INVALID_INDEX;
    bool expects_skin = false;
    if (skinned_path && node->skin != nullptr) {
        auto it = skin_to_index.find(node->skin);
        if (it != skin_to_index.end()) {
            skin_index = it->second;
            expects_skin = true;
        }
    }

    for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
        const cgltf_primitive* primitive = &node->mesh->primitives[p];
        if (primitive->type != cgltf_primitive_type_triangles) {
            result.warnings.push_back("Skipping non-triangle primitive in node " + safe_name(node->name, "Node"));
            continue;
        }

        const cgltf_accessor* positions = find_attr(primitive, cgltf_attribute_type_position, 0);
        if (!positions || positions->count == 0) {
            result.warnings.push_back("Skipping primitive without POSITION in node " + safe_name(node->name, "Node"));
            continue;
        }

        const cgltf_accessor* normals = find_attr(primitive, cgltf_attribute_type_normal, 0);
        const cgltf_accessor* tangents = find_attr(primitive, cgltf_attribute_type_tangent, 0);
        const cgltf_accessor* uv0 = find_attr(primitive, cgltf_attribute_type_texcoord, 0);
        const cgltf_accessor* color0 = find_attr(primitive, cgltf_attribute_type_color, 0);
        const cgltf_accessor* joints0 = find_attr(primitive, cgltf_attribute_type_joints, 0);
        const cgltf_accessor* weights0 = find_attr(primitive, cgltf_attribute_type_weights, 0);

        uint32_t material_index = ensure_material(primitive->material,
                                                  model_path,
                                                  result,
                                                  material_cache,
                                                  texture_cache);

        std::vector<uint32_t> indices;
        if (primitive->indices && primitive->indices->count > 0) {
            indices.resize(primitive->indices->count);
            cgltf_size unpacked = cgltf_accessor_unpack_indices(primitive->indices,
                                                                indices.data(),
                                                                sizeof(uint32_t),
                                                                primitive->indices->count);
            if (unpacked != primitive->indices->count) {
                result.warnings.push_back("Skipping primitive with failed index unpack in node " + safe_name(node->name, "Node"));
                continue;
            }
        } else {
            indices.resize(positions->count);
            for (cgltf_size i = 0; i < positions->count; ++i) {
                indices[i] = static_cast<uint32_t>(i);
            }
        }

        uint32_t max_index = 0;
        for (uint32_t idx : indices) {
            max_index = std::max(max_index, idx);
        }
        if (!indices.empty() && max_index >= positions->count) {
            result.warnings.push_back(
                "Skipping primitive with out-of-range index in node " + safe_name(node->name, "Node") +
                " (max index " + std::to_string(max_index) +
                ", vertex count " + std::to_string(static_cast<uint32_t>(positions->count)) + ")");
            continue;
        }

        MeshPrimitive mesh_primitive = {};
        mesh_primitive.instance_index = instance_index;
        mesh_primitive.material_index = material_index;
        mesh_primitive.first_index = 0;
        mesh_primitive.index_count = static_cast<uint32_t>(indices.size());
        mesh_primitive.skinned = 0;
        mesh_primitive.skin_index = MODEL_LOADER_INVALID_INDEX;

        if (skinned_path) {
            std::vector<CpuSkinnedVertex> vertices(positions->count);

            bool warned_missing_joints = false;
            for (cgltf_size v = 0; v < positions->count; ++v) {
                CpuSkinnedVertex vertex = {};

                float p3[3] = {0.0f, 0.0f, 0.0f};
                float n3[3] = {0.0f, 1.0f, 0.0f};
                float t4[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                float uv[2] = {0.0f, 0.0f};
                float c4[4] = {1.0f, 1.0f, 1.0f, 1.0f};

                read_floats(positions, v, p3, 3);
                read_floats(normals, v, n3, 3);
                read_floats(tangents, v, t4, 4);
                read_floats(uv0, v, uv, 2);
                if (color0) {
                    uint32_t comps = static_cast<uint32_t>(cgltf_num_components(color0->type));
                    if (comps == 3) {
                        float c3[3] = {1.0f, 1.0f, 1.0f};
                        read_floats(color0, v, c3, 3);
                        c4[0] = c3[0];
                        c4[1] = c3[1];
                        c4[2] = c3[2];
                    } else {
                        read_floats(color0, v, c4, 4);
                    }
                }

                transform_point(world, p3, vertex.position);
                vertex.position[3] = 1.0f;
                transform_direction_with_normal_matrix(world, n3, vertex.normal);
                vertex.normal[3] = 0.0f;
                float tangent_xyz[3] = {t4[0], t4[1], t4[2]};
                transform_direction_with_normal_matrix(world, tangent_xyz, tangent_xyz);
                vertex.tangent[0] = tangent_xyz[0];
                vertex.tangent[1] = tangent_xyz[1];
                vertex.tangent[2] = tangent_xyz[2];
                vertex.tangent[3] = t4[3];

                vertex.uv0[0] = uv[0];
                vertex.uv0[1] = uv[1];
                vertex.uv0[2] = 0.0f;
                vertex.uv0[3] = 0.0f;
                vertex.color0[0] = c4[0];
                vertex.color0[1] = c4[1];
                vertex.color0[2] = c4[2];
                vertex.color0[3] = c4[3];

                if (!read_joint_indices(joints0, v, vertex.joints)) {
                    vertex.joints[0] = 0;
                    vertex.joints[1] = 0;
                    vertex.joints[2] = 0;
                    vertex.joints[3] = 0;
                    if (expects_skin && !warned_missing_joints) {
                        result.warnings.push_back("Skinned primitive is missing JOINTS_0 in node " + safe_name(node->name, "Node"));
                        warned_missing_joints = true;
                    }
                }
                fill_weights_or_default(weights0, v, vertex.weights);

                vertices[v] = vertex;
            }

            std::string vb_name = safe_name(node->name, "Node") + " Vertex Buffer";
            std::string ib_name = safe_name(node->name, "Node") + " Index Buffer";

            mesh_primitive.vertex_buffer_job_index = append_buffer_job(result,
                                                                       MeshBufferKind::Vertex,
                                                                       LUMINARY_RHI_BUFFER_USAGE_SHADER_READ,
                                                                       vb_name,
                                                                       vertices);
            mesh_primitive.index_buffer_job_index = append_index_buffer_job_u32(result,
                                                                                ib_name,
                                                                                indices);
            mesh_primitive.vertex_stride = static_cast<uint32_t>(sizeof(CpuSkinnedVertex));
            mesh_primitive.skinned = expects_skin ? 1u : 0u;
            mesh_primitive.skin_index = expects_skin ? skin_index : MODEL_LOADER_INVALID_INDEX;
        } else {
            std::vector<CpuVertex> vertices(positions->count);

            for (cgltf_size v = 0; v < positions->count; ++v) {
                CpuVertex vertex = {};

                float p3[3] = {0.0f, 0.0f, 0.0f};
                float n3[3] = {0.0f, 1.0f, 0.0f};
                float t4[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                float uv[2] = {0.0f, 0.0f};
                float c4[4] = {1.0f, 1.0f, 1.0f, 1.0f};

                read_floats(positions, v, p3, 3);
                read_floats(normals, v, n3, 3);
                read_floats(tangents, v, t4, 4);
                read_floats(uv0, v, uv, 2);
                if (color0) {
                    uint32_t comps = static_cast<uint32_t>(cgltf_num_components(color0->type));
                    if (comps == 3) {
                        float c3[3] = {1.0f, 1.0f, 1.0f};
                        read_floats(color0, v, c3, 3);
                        c4[0] = c3[0];
                        c4[1] = c3[1];
                        c4[2] = c3[2];
                    } else {
                        read_floats(color0, v, c4, 4);
                    }
                }

                transform_point(world, p3, vertex.position);
                vertex.position[3] = 1.0f;
                transform_direction_with_normal_matrix(world, n3, vertex.normal);
                vertex.normal[3] = 0.0f;
                float tangent_xyz[3] = {t4[0], t4[1], t4[2]};
                transform_direction_with_normal_matrix(world, tangent_xyz, tangent_xyz);
                vertex.tangent[0] = tangent_xyz[0];
                vertex.tangent[1] = tangent_xyz[1];
                vertex.tangent[2] = tangent_xyz[2];
                vertex.tangent[3] = t4[3];

                vertex.uv0[0] = uv[0];
                vertex.uv0[1] = uv[1];
                vertex.uv0[2] = 0.0f;
                vertex.uv0[3] = 0.0f;
                vertex.color0[0] = c4[0];
                vertex.color0[1] = c4[1];
                vertex.color0[2] = c4[2];
                vertex.color0[3] = c4[3];

                vertices[v] = vertex;
            }

            std::string vb_name = safe_name(node->name, "Node") + " Vertex Buffer";
            std::string ib_name = safe_name(node->name, "Node") + " Index Buffer";

            mesh_primitive.vertex_buffer_job_index = append_buffer_job(result,
                                                                       MeshBufferKind::Vertex,
                                                                       LUMINARY_RHI_BUFFER_USAGE_SHADER_READ,
                                                                       vb_name,
                                                                       vertices);
            mesh_primitive.index_buffer_job_index = append_index_buffer_job_u32(result,
                                                                                ib_name,
                                                                                indices);
            mesh_primitive.vertex_stride = static_cast<uint32_t>(sizeof(CpuVertex));
        }

        result.mesh_primitives.push_back(mesh_primitive);
    }
}

static void traverse_node(const cgltf_node* node,
                          uint32_t parent_instance_index,
                          bool skinned_path,
                          const std::unordered_map<const cgltf_skin*, uint32_t>& skin_to_index,
                          const std::filesystem::path& model_path,
                          std::unordered_map<const cgltf_material*, uint32_t>& material_cache,
                          std::unordered_map<LogicalTextureKey, uint32_t, LogicalTextureKeyHash, LogicalTextureKeyEq>& texture_cache,
                          const std::unordered_map<const cgltf_node*, uint32_t>& node_to_index,
                          Mesh_Result& result)
{
    ModelInstance instance = {};
    instance.name = safe_name(node->name, "Node");
    auto node_it = node_to_index.find(node);
    instance.node_index = node_it != node_to_index.end() ? node_it->second : MODEL_LOADER_INVALID_INDEX;
    instance.parent_instance_index = parent_instance_index;

    float world[16] = {};
    cgltf_node_transform_world(node, world);
    copy16(world, instance.world_transform);

    instance.first_primitive_index = static_cast<uint32_t>(result.mesh_primitives.size());
    instance.primitive_count = 0;

    uint32_t this_instance_index = static_cast<uint32_t>(result.model_instances.size());
    result.model_instances.push_back(instance);

    size_t primitive_count_before = result.mesh_primitives.size();
    extract_node_primitives(node,
                            this_instance_index,
                            skinned_path,
                            skin_to_index,
                            model_path,
                            material_cache,
                            texture_cache,
                            result);
    size_t primitive_count_after = result.mesh_primitives.size();

    result.model_instances[this_instance_index].primitive_count =
        static_cast<uint32_t>(primitive_count_after - primitive_count_before);

    for (cgltf_size i = 0; i < node->children_count; ++i) {
        traverse_node(node->children[i],
                      this_instance_index,
                      skinned_path,
                      skin_to_index,
                      model_path,
                      material_cache,
                      texture_cache,
                      node_to_index,
                      result);
    }
}

static Mesh_Result load_mesh_internal(const char* path, bool skinned_path)
{
    Mesh_Result result = {};
    result.success = 0;

    if (!path || !path[0]) {
        result.error = "Path is empty";
        return result;
    }

    std::filesystem::path model_path(path);

    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result parse_result = cgltf_parse_file(&options, path, &data);
    if (parse_result != cgltf_result_success || !data) {
        result.error = "cgltf_parse_file failed";
        return result;
    }

    cgltf_result load_result = cgltf_load_buffers(&options, data, path);
    if (load_result != cgltf_result_success) {
        result.error = "cgltf_load_buffers failed";
        cgltf_free(data);
        return result;
    }

    cgltf_result validate_result = cgltf_validate(data);
    if (validate_result != cgltf_result_success) {
        result.warnings.push_back("cgltf_validate returned warnings/errors; continuing best effort");
    }

    std::unordered_map<const cgltf_node*, uint32_t> node_to_index;
    node_to_index.reserve(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        node_to_index.emplace(&data->nodes[i], static_cast<uint32_t>(i));
    }

    extract_animations(data, node_to_index, result);
    extract_skins(data, node_to_index, result);

    std::unordered_map<const cgltf_skin*, uint32_t> skin_to_index;
    skin_to_index.reserve(data->skins_count);
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        skin_to_index.emplace(&data->skins[i], static_cast<uint32_t>(i));
    }

    std::unordered_map<const cgltf_material*, uint32_t> material_cache;
    std::unordered_map<LogicalTextureKey, uint32_t, LogicalTextureKeyHash, LogicalTextureKeyEq> texture_cache;

    // Ensure there is always a default material at index 0.
    ensure_material(nullptr, model_path, result, material_cache, texture_cache);

    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
            traverse_node(data->scene->nodes[i],
                          MODEL_LOADER_INVALID_INDEX,
                          skinned_path,
                          skin_to_index,
                          model_path,
                          material_cache,
                          texture_cache,
                          node_to_index,
                          result);
        }
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent != nullptr) {
                continue;
            }

            traverse_node(&data->nodes[i],
                          MODEL_LOADER_INVALID_INDEX,
                          skinned_path,
                          skin_to_index,
                          model_path,
                          material_cache,
                          texture_cache,
                          node_to_index,
                          result);
        }
    }

    if (result.mesh_primitives.empty()) {
        result.error = "No mesh primitives were extracted";
        cgltf_free(data);
        return result;
    }

    result.success = 1;
    cgltf_free(data);
    return result;
}

} // namespace

Mesh_Result load_mesh(const char* path)
{
    return load_mesh_internal(path, false);
}

Mesh_Result load_skinned_mesh(const char* path)
{
    return load_mesh_internal(path, true);
}
