#include "tests/test.h"

class submit_empty_command_buffer_test : public test
{
    LRHIDevice _device = nullptr;

public:
    submit_empty_command_buffer_test()
    {
        type = test_type::validation;
        name = "submit_empty_command_buffer";
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

        LRHICommandList command_list = nullptr;
        err = {};
        lrhi_create_command_list(queue, &command_list, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || !command_list) {
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "failed to create command list" };
        }

        err = {};
        lrhi_command_list_begin(command_list, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || err.severity == LUMINARY_RHI_ERROR_SEVERITY_WARNING) {
            lrhi_destroy_command_list(command_list);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "command list begin failed or returned warning" };
        }

        err = {};
        lrhi_command_list_end(command_list, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || err.severity == LUMINARY_RHI_ERROR_SEVERITY_WARNING) {
            lrhi_destroy_command_list(command_list);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "command list end failed or returned warning" };
        }

        err = {};
        lrhi_command_queue_submit(queue, &command_list, 1, fence, 1, nullptr, 0, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || err.severity == LUMINARY_RHI_ERROR_SEVERITY_WARNING) {
            lrhi_destroy_command_list(command_list);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "empty command list submission returned warning/error" };
        }

        err = {};
        lrhi_fence_wait(fence, 1, 5000000000ULL, &err);
        if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR || err.severity == LUMINARY_RHI_ERROR_SEVERITY_WARNING) {
            lrhi_destroy_command_list(command_list);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "fence wait after empty submission failed or timed out" };
        }

        if (lrhi_fence_get_value(fence) < 1) {
            lrhi_destroy_command_list(command_list);
            lrhi_destroy_fence(fence);
            lrhi_destroy_command_queue(queue);
            return { false, "fence value did not reach expected value 1" };
        }

        lrhi_destroy_command_list(command_list);
        lrhi_destroy_fence(fence);
        lrhi_destroy_command_queue(queue);
        return { true, "" };
    }

    void cleanup() override {}
};

REGISTER_TEST(submit_empty_command_buffer_test);