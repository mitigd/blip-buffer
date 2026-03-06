const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const sdl_dep = b.dependency("sdl", .{
        .target = target,
        .optimize = optimize,
    });

    const Renderer = enum { Vulkan, OpenGL3, Metal };
    const Platform = enum { GLFW, SDL3, SDLGPU3 };

    const cimgui_dep = b.dependency("cimgui_zig", .{
        .target = target,
        .optimize = optimize,
        .platforms = @as([]const Platform, &.{.SDL3}),
        .renderers = @as([]const Renderer, &.{.OpenGL3}),
    });

    const lib_mod = b.createModule(.{
        .root_source_file = b.path("src/lib.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });

    const blip_lib = b.addLibrary(.{
        .name = "blip_buffer",
        .linkage = .static,
        .root_module = lib_mod,
    });

    blip_lib.root_module.addIncludePath(b.path("."));
    blip_lib.root_module.addIncludePath(b.path("src"));
    blip_lib.root_module.addCSourceFiles(.{
        .root = b.path("."),
        .files = &.{
            "Blip_Buffer.cpp",
            "src/blip_demo_bridge.cpp",
        },
        .flags = &.{"-std=c++17"},
    });
    blip_lib.installHeader(b.path("Blip_Buffer.h"), "Blip_Buffer.h");
    blip_lib.installHeader(b.path("src/blip_demo_bridge.h"), "blip_demo_bridge.h");
    b.installArtifact(blip_lib);

    const exe = b.addExecutable(.{
        .name = "blip-demo-lab",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .link_libcpp = true,
        }),
    });

    exe.root_module.addIncludePath(b.path("src"));
    exe.linkLibrary(blip_lib);
    exe.linkLibrary(sdl_dep.artifact("SDL3"));
    exe.linkLibrary(cimgui_dep.artifact("cimgui"));

    if (target.result.os.tag == .windows) {
        exe.root_module.linkSystemLibrary("opengl32", .{});
    } else if (target.result.os.tag == .macos) {
        exe.root_module.linkFramework("OpenGL", .{});
        exe.root_module.linkFramework("Cocoa", .{});
        exe.root_module.linkFramework("IOKit", .{});
        exe.root_module.linkFramework("CoreVideo", .{});
    } else {
        exe.root_module.linkSystemLibrary("GL", .{});
    }

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the SDL3 + ImGui demo lab");
    run_step.dependOn(&run_cmd.step);
}
