#include "luminary_rhi.h"
#include "test.h"

#include <cstdio>
#include <cstring>
#include <fstream>

#include "json/json.hpp"

std::string backend_to_string(LRHIBackend backend)
{
    switch (backend) {
        case LUMINARY_RHI_BACKEND_VULKAN: return "VULKAN";
        case LUMINARY_RHI_BACKEND_D3D12: return "D3D12";
        case LUMINARY_RHI_BACKEND_METAL3: return "METAL3";
        case LUMINARY_RHI_BACKEND_METAL4: return "METAL4";
        default: return "UNKNOWN";
    }
}

int main(int argc, char** argv)
{
    bool bake_mode = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bake") == 0)
            bake_mode = true;
    }

    if (bake_mode)
        printf("Running in BAKE mode — golden files will be (re)generated.\n");

    // Create device
    LRHIError  err    = {};
    LRHIDevice device = nullptr;
    lrhi_create_device(LUMINARY_RHI_BACKEND_METAL3, &device, 1, &err);
    if (err.severity == LUMINARY_RHI_ERROR_SEVERITY_ERROR) {
        fprintf(stderr, "Failed to create device: %s\n", err.message);
        return 1;
    }
    if (!device) {
        fprintf(stderr, "Failed to create device (null handle)\n");
        return 1;
    }

    LRHIDeviceInfo dev_info = lrhi_get_device_info(device);
    printf("Device: %s  Backend: %s\n", dev_info.device_name, backend_to_string(dev_info.backend).c_str());

    // Collect all self-registered tests
    auto tests = test_registry::create_all();

    // Run tests and collect results
    nlohmann::json results_json;
    results_json["device"]    = dev_info.device_name;
    results_json["backend"]   = backend_to_string(dev_info.backend);
    results_json["bake_mode"] = bake_mode;
    results_json["tests"]     = nlohmann::json::array();

    int passed_count = 0;
    int total_count  = (int)tests.size();

    for (auto& t : tests) {
#if __APPLE__
        @autoreleasepool {
#else
        {
#endif
            printf("  [%s] %s ... ", bake_mode ? "BAKE" : "RUN ", t->name);
            fflush(stdout);

            t->init(device);
            test_result r = t->run(bake_mode);
            t->cleanup();

            printf("%s\n", r.passed ? "PASS" : "FAIL");
            if (!r.message.empty() && r.message != "baked")
                printf("         %s\n", r.message.c_str());

            if (r.passed) passed_count++;

            const char* type_str = (t->type == test_type::validation) ? "validation"
                                 : (t->type == test_type::texture)    ? "texture"
                                                                      : "buffer";

            nlohmann::json entry;
            entry["name"]    = t->name;
            entry["type"]    = type_str;
            entry["passed"]  = r.passed;
            entry["message"] = r.message;

            if (t->type == test_type::texture) {
                entry["flip_mean_error"] = r.flip_mean_error;
                if (!r.output_image.empty()) entry["output_image"] = r.output_image;
                if (!r.golden_image.empty()) entry["golden_image"] = r.golden_image;
                if (!r.flip_image.empty())   entry["flip_image"]   = r.flip_image;
            }

            if (t->type == test_type::buffer) {
                if (!r.output_buffer.empty()) entry["output_buffer"] = r.output_buffer;
                if (!r.golden_buffer.empty()) entry["golden_buffer"] = r.golden_buffer;
            }

            results_json["tests"].push_back(entry);
        }
    }

    printf("\nResults: %d / %d passed\n", passed_count, total_count);

    // Write results.json to the project root (xmake rundir is ".")
    {
        std::ofstream out("results.json");
        if (out) {
            out << results_json.dump(2) << "\n";
            printf("Results written to results.json\n");
        } else {
            fprintf(stderr, "Failed to write results.json\n");
        }
    }

    lrhi_destroy_device(device);
    return (passed_count == total_count) ? 0 : 1;
}
