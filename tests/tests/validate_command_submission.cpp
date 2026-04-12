#include "tests/test.h"

class validate_command_submission_test : public test
{
    LRHIDevice _device = nullptr;

public:
    validate_command_submission_test()
    {
        type = test_type::validation;
        name = "validate_command_submission";
        source_path = nullptr;
    }

    void init(LRHIDevice device) override
    {
        _device = device;
    }

    test_result run(bool /*bake_mode*/) override
    {
        LRHIError err = {};

        LRHICommandQueue queue = nullptr;
        lrhi_create_command_queue(_device, &queue, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !queue)
            return { false, "failed to create command queue" };

        LRHIFence fence = nullptr;
        err = {};
        lrhi_create_fence(_device, 0, &fence, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !fence) {
            lrhi_destroy_command_queue(queue);
            return { false, "failed to create fence" };
        }

        err = {};
        lrhi_command_queue_signal(queue, fence, 1, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "command queue signal failed" };
        }

        err = {};
        lrhi_fence_wait(fence, 1, 5000000000ULL, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || err.severity == LUMINARY_RHI_ERROR_SEVERITY_WARNING) {
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "fence wait for value 1 failed or timed out" };
        }

        if (lrhi_fence_get_value(fence) < 1) {
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "fence value is below expected value 1" };
        }

        err = {};
        lrhi_fence_signal(fence, 2, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "cpu fence signal failed" };
        }

        if (lrhi_fence_get_value(fence) < 2) {
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "fence value is below expected value 2" };
        }

        LRHIDeviceInfo info = lrhi_get_device_info(_device);
        if (info.backend == LUMINARY_RHI_BACKEND_METAL4) {
            err = {};
            lrhi_command_queue_wait(queue, fence, 2, 5000000000ULL, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || err.severity == LUMINARY_RHI_ERROR_SEVERITY_WARNING) {
                lrhi_destroy_fence(fence);
                lrhi_destroy_command_queue(queue);
                return { false, "metal4 command queue wait failed or timed out" };
            }

            err = {};
            lrhi_command_queue_signal(queue, fence, 3, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
                lrhi_destroy_fence(fence);
                lrhi_destroy_command_queue(queue);
                return { false, "metal4 command queue signal for value 3 failed" };
            }

            err = {};
            lrhi_fence_wait(fence, 3, 5000000000ULL, &err);
            if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || err.severity == LUMINARY_RHI_ERROR_SEVERITY_WARNING) {
                lrhi_destroy_fence(fence);
                lrhi_destroy_command_queue(queue);
                return { false, "metal4 fence wait for value 3 failed or timed out" };
            }

            if (lrhi_fence_get_value(fence) < 3) {
                lrhi_destroy_fence(fence);
                lrhi_destroy_command_queue(queue);
                return { false, "fence value is below expected value 3 on metal4" };
            }
        }

        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        return { true, "" };
    }

    void cleanup() override {}
};

REGISTER_TEST(validate_command_submission_test);
