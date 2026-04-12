add_rules("mode.debug", "mode.release", "mode.releasedbg")

set_languages("c99", "c++17")
set_rundir(".")
add_includedirs("src", ".", { public = true })

if is_plat("windows") then
    add_defines("LRHI_WINDOWS", { public = true })
elseif is_plat("linux") then
    add_defines("LRHI_LINUX", { public = true })
elseif is_plat("macosx") then
    add_defines("LRHI_MACOS", { public = true })
    add_cflags("-x objective-c", { public = true })
    add_frameworks("Foundation", "Metal")
end

target("luminary_rhi")
    set_kind("static")
    add_headerfiles("src/*.h")

    if is_plat("macosx") then
        add_files("src/luminary_rhi_metal4.m")
    end

target("examples")
    set_kind("binary")
    add_files("examples/*.c")
    add_deps("luminary_rhi")

target("tests")
    set_kind("binary")
    add_files("tests/**.cpp")
    add_deps("luminary_rhi")
