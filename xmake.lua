add_rules("mode.debug", "mode.release", "mode.releasedbg")

set_rundir(".")
add_includedirs("src", ".", { public = true })

if is_plat("windows") then
    add_defines("LRHI_WINDOWS", { public = true })
    add_syslinks("dxgi", "d3d12")
elseif is_plat("linux") then
    add_defines("LRHI_LINUX", { public = true })
    add_requires("glfw", { system = true })
elseif is_plat("macosx") then
    add_defines("LRHI_MACOS", { public = true })
    add_cflags("-x objective-c", "-fobjc-arc", { public = true })
    add_cxxflags("-x objective-c++", "-fobjc-arc", { public = true })
    add_frameworks("Foundation", "Metal")
end

target("luminary_rhi")
    set_kind("static")
    add_headerfiles("src/*.h")
    add_files("src/luminary_rhi.c")
    add_files("src/luminary_rhi_internal.c")

    if is_plat("macosx") then
        add_files("src/luminary_rhi_metal3.m")
        add_files("src/luminary_rhi_metal4.m")
    elseif is_plat("windows") then
        add_files("src/luminary_rhi_d3d12.c")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "COBJMACROS", "CINTERFACE")
        add_syslinks("bin/WinPixEventRuntime.lib")
    elseif is_plat("linux") then
        add_files("src/luminary_rhi_vulkan.c")
    end

target("examples")
    set_languages("c++17")
    set_kind("binary")
    add_files("examples/*.cpp",
              "examples/ext/imgui/*.cpp",
              "examples/examples/*.cpp",
              "examples/ext/cgltf/*.cpp",
              "examples/ext/stb/*.cpp",
              "examples/ext/meshoptimizer/*.cpp")
    add_files("examples/ext/imgui/backends/imgui_impl_luminary.cpp")
    add_deps("luminary_rhi", "shader_compiler")
    add_includedirs("extras/shader_compiler")

    if is_plat("macosx") then
        add_frameworks("Foundation", "Metal", "QuartzCore", "Cocoa", "GameController")
        add_rpathdirs("bin/")
        add_syslinks("bin/libdxcompiler.dylib", "bin/libmetalirconverter.dylib")
        add_files("examples/ext/imgui/backends/imgui_impl_osx.mm", "examples/platform/window_macos.mm")
    elseif is_plat("windows") then
        add_syslinks("dxgi", "d3d12", "dxguid")
        add_files("examples/ext/imgui/backends/imgui_impl_win32.cpp", "examples/platform/window_win32.cpp")
        after_build(function (target)
            os.cp("bin/dxcompiler.dll", path.join(target:targetdir(), "dxcompiler.dll"))
            os.cp("bin/dxil.dll", path.join(target:targetdir(), "dxil.dll"))
            os.cp("bin/WinPixEventRuntime.dll", path.join(target:targetdir(), "WinPixEventRuntime.dll"))
            os.cp("bin/D3D12Core.dll", path.join(target:targetdir(), "D3D12Core.dll"))
            os.cp("bin/d3d12SDKLayers.dll", path.join(target:targetdir(), "d3d12SDKLayers.dll"))
        end)
    elseif is_plat("linux") then
        add_files("examples/platform/window_linux.cpp", "examples/ext/imgui/backends/imgui_impl_glfw.cpp")
        add_packages("glfw")
        add_rpathdirs("bin/")
    end

target("shader_compiler")
    set_languages("c++17")
    set_kind("static")
    add_includedirs("extras/shader_compiler")
    add_files("extras/shader_compiler/luminary_shader_compiler.cpp")

    if is_plat("macosx") then
        add_files("extras/shader_compiler/luminary_shader_compiler_msl.mm")
        add_files("extras/shader_compiler/luminary_shader_compiler_msc.mm")
    elseif is_plat("windows") then
        add_files("extras/shader_compiler/luminary_shader_compiler_dxil.cpp")
        add_files("extras/shader_compiler/luminary_shader_compiler_spirv.cpp")
    end

target("tests")
    set_kind("binary")
    set_languages("c++17")
    add_files("tests/**.cpp")
    add_deps("luminary_rhi", "shader_compiler")
    add_includedirs("extras/shader_compiler")

    if is_plat("macosx") then
        add_rpathdirs("bin/")
        add_syslinks("bin/libdxcompiler.dylib", "bin/libmetalirconverter.dylib")
    elseif is_plat("windows") then
        add_syslinks("dxgi", "d3d12", "dxguid")
        after_build(function (target)
            os.cp("bin/dxcompiler.dll", path.join(target:targetdir(), "dxcompiler.dll"))
            os.cp("bin/dxil.dll", path.join(target:targetdir(), "dxil.dll"))
            os.cp("bin/WinPixEventRuntime.dll", path.join(target:targetdir(), "WinPixEventRuntime.dll"))
            os.cp("bin/D3D12Core.dll", path.join(target:targetdir(), "D3D12Core.dll"))
            os.cp("bin/d3d12SDKLayers.dll", path.join(target:targetdir(), "d3d12SDKLayers.dll"))
        end)
    elseif is_plat("linux") then
        add_rpathdirs("bin/")
    end
