#include <luminary_rhi.h>
#include <stdio.h>

int main(void)
{
    LRHIDevice device;
    lrhi_create_device(LUMINARY_RHI_BACKEND_METAL4, &device, 0, NULL);

    LRHIDeviceInfo device_info = lrhi_get_device_info(device);
    printf("Device Name: %s\n", device_info.device_name);
    printf("Backend: %d\n", device_info.backend);
    printf("Features:\n");
    printf("  Ray Tracing: %s\n", device_info.features.ray_tracing ? "Yes" : "No");
    printf("  Mesh Shading: %s\n", device_info.features.mesh_shading ? "Yes" : "No");
    printf("  Bindless Resources: %s\n", device_info.features.bindless_resources ? "Yes" : "No");
    printf("  Multi Draw Indirect: %s\n", device_info.features.multi_draw_indirect ? "Yes" : "No");
    printf("Limits:\n");
    printf("  Max Texture Dimension 2D: %u\n", device_info.limits.max_texture_dimension_2d);
    printf("  Max Texture Dimension 3D: %u\n", device_info.limits.max_texture_dimension_3d);
    printf("  Max Texture Array Layers: %u\n", device_info.limits.max_texture_array_layers);
    printf("  Max Buffer Size: %u\n", device_info.limits.max_buffer_size);

    lrhi_destroy_device(device);
}
