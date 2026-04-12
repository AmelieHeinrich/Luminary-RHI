#include "tests/test.h"

class validate_device_test : public test
{
    LRHIDevice _device = nullptr;

public:
    validate_device_test()
    {
        type        = test_type::validation;
        name        = "validate_device";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override
    {
        _device = device;
    }

    test_result run(bool /*bake_mode*/) override
    {
        LRHIDeviceInfo info = lrhi_get_device_info(_device);

        if (info.device_name[0] == '\0')
            return { false, "device_name is empty" };
        if (info.limits.max_texture_dimension_2d == 0)
            return { false, "max_texture_dimension_2d is 0" };
        if (info.limits.max_texture_dimension_3d == 0)
            return { false, "max_texture_dimension_3d is 0" };
        if (info.limits.max_texture_array_layers == 0)
            return { false, "max_texture_array_layers is 0" };
        if (info.limits.max_buffer_size == 0)
            return { false, "max_buffer_size is 0" };

        return { true, "" };
    }

    void cleanup() override {}
};

REGISTER_TEST(validate_device_test);
