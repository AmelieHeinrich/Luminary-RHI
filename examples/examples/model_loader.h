#pragma once

#include "luminary_rhi.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

static constexpr uint32_t MODEL_LOADER_INVALID_INDEX = 0xFFFFFFFFu;

enum class MeshBufferKind : uint8_t
{
    Vertex,
    Index,
    SkinMatrices,
    Material,
    Instance,
};

enum class AnimationTargetPath : uint8_t
{
    Translation,
    Rotation,
    Scale,
    Weights,
};

enum class AnimationInterpolation : uint8_t
{
    Linear,
    Step,
    CubicSpline,
};

struct BufferUploadJob
{
    MeshBufferKind kind;
    LRHIBufferUsage usage;
    uint64_t stride;
    std::string debug_name;
    std::vector<uint8_t> bytes;
};

struct TextureUploadJob
{
    std::string debug_name;
    uint32_t width;
    uint32_t height;
    LRHITextureFormat format;
    uint32_t bytes_per_row;
    uint32_t bytes_per_image;
    std::vector<uint8_t> pixels_rgba8;
};

struct ModelTexture
{
    std::string name;
    uint32_t upload_job_index;
    uint8_t srgb;
};

struct MaterialTextureSlot
{
    uint32_t texture_index;
    int32_t texcoord_set;
    float scale;
    uint8_t has_transform;
    float offset[2];
    float uv_scale[2];
    float rotation_radians;
};

struct ModelMaterial
{
    std::string name;
    float base_color_factor[4];
    float emissive_factor[3];
    float metallic_factor;
    float roughness_factor;
    float alpha_cutoff;
    uint8_t alpha_mode;
    uint8_t double_sided;
    uint8_t unlit;

    MaterialTextureSlot base_color;
    MaterialTextureSlot normal;
    MaterialTextureSlot metallic_roughness;
    MaterialTextureSlot occlusion;
    MaterialTextureSlot emissive;
};

struct ModelSkin
{
    std::string name;
    uint32_t skeleton_root_node_index;
    std::vector<uint32_t> joint_node_indices;
    std::vector<float> inverse_bind_matrices;
};

struct MeshPrimitive
{
    uint32_t instance_index;
    uint32_t material_index;
    uint32_t vertex_buffer_job_index;
    uint32_t index_buffer_job_index;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t vertex_stride;
    uint8_t skinned;
    uint32_t skin_index;
};

struct ModelInstance
{
    std::string name;
    uint32_t node_index;
    uint32_t parent_instance_index;
    float world_transform[16];
    uint32_t first_primitive_index;
    uint32_t primitive_count;
};

struct AnimationSampler
{
    AnimationInterpolation interpolation;
    std::vector<float> input_times;
    std::vector<float> output_values;
    uint32_t output_components;
};

struct AnimationChannel
{
    uint32_t sampler_index;
    uint32_t target_node_index;
    AnimationTargetPath target_path;
};

struct AnimationClip
{
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    float start_time;
    float end_time;
};

struct Mesh_Result
{
    uint8_t success;
    std::string error;
    std::vector<std::string> warnings;

    std::vector<BufferUploadJob> buffer_upload_jobs;
    std::vector<TextureUploadJob> texture_upload_jobs;

    std::vector<ModelTexture> model_textures;
    std::vector<ModelMaterial> model_materials;
    std::vector<ModelSkin> model_skins;
    std::vector<ModelInstance> model_instances;
    std::vector<MeshPrimitive> mesh_primitives;

    std::vector<std::optional<AnimationClip>> optional_animations;
};

Mesh_Result load_mesh(const char* path);
Mesh_Result load_skinned_mesh(const char* path);
