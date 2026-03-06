const std = @import("std");

const c = @cImport({
    @cInclude("SDL3/SDL.h");
    @cInclude("SDL3/SDL_opengl.h");
    @cInclude("backends/dcimgui_impl_sdl3.h");
    @cInclude("backends/dcimgui_impl_opengl3.h");
    @cInclude("blip_demo_bridge.h");
});

const preview_capacity = 512;
const audio_frames = 1024;
const ui_bg_alpha: f32 = 0.76;

const App = struct {
    window: ?*c.SDL_Window = null,
    gl_context: c.SDL_GLContext = null,
    audio_stream: ?*c.SDL_AudioStream = null,
    ctx: ?*c.BlipContext = null,
    settings: c.BlipDemoSettings = undefined,
    preview: [preview_capacity]f32 = [_]f32{0} ** preview_capacity,
    preview_len: c_int = 0,
    stereo_buffer: [audio_frames * 2]c_short = [_]c_short{0} ** (audio_frames * 2),
    current_demo: c_int = c.BLIP_DEMO_WAVEFORM,
    running: bool = true,
};

var app_global: App = .{};

fn sdlErr() []const u8 {
    return std.mem.span(c.SDL_GetError());
}

fn ensure(ok: bool) !void {
    if (!ok) return error.SdlFailure;
}

fn initWindow(app: *App) !void {
    if (!c.SDL_Init(c.SDL_INIT_VIDEO | c.SDL_INIT_AUDIO)) {
        std.log.err("SDL_Init failed: {s}", .{sdlErr()});
        return error.SdlFailure;
    }

    _ = c.SDL_GL_SetAttribute(c.SDL_GL_CONTEXT_FLAGS, 0);
    _ = c.SDL_GL_SetAttribute(c.SDL_GL_CONTEXT_PROFILE_MASK, c.SDL_GL_CONTEXT_PROFILE_CORE);
    _ = c.SDL_GL_SetAttribute(c.SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    _ = c.SDL_GL_SetAttribute(c.SDL_GL_CONTEXT_MINOR_VERSION, 0);
    _ = c.SDL_GL_SetAttribute(c.SDL_GL_DOUBLEBUFFER, 1);

    app.window = c.SDL_CreateWindow(
        "Blip Buffer Demo Lab",
        1280,
        800,
        c.SDL_WINDOW_OPENGL | c.SDL_WINDOW_RESIZABLE | c.SDL_WINDOW_HIGH_PIXEL_DENSITY,
    );
    if (app.window == null) {
        std.log.err("SDL_CreateWindow failed: {s}", .{sdlErr()});
        return error.SdlFailure;
    }

    app.gl_context = c.SDL_GL_CreateContext(app.window);
    if (app.gl_context == null) {
        std.log.err("SDL_GL_CreateContext failed: {s}", .{sdlErr()});
        return error.SdlFailure;
    }

    try ensure(c.SDL_GL_MakeCurrent(app.window, app.gl_context));
    _ = c.SDL_GL_SetSwapInterval(1);
}

fn initAudio(app: *App) !void {
    var spec = c.SDL_AudioSpec{
        .format = c.SDL_AUDIO_S16,
        .channels = 2,
        .freq = app.settings.sample_rate,
    };

    app.audio_stream = c.SDL_OpenAudioDeviceStream(c.SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, null, null);
    if (app.audio_stream == null) {
        std.log.err("SDL_OpenAudioDeviceStream failed: {s}", .{sdlErr()});
        return error.SdlFailure;
    }
    _ = c.SDL_ResumeAudioStreamDevice(app.audio_stream);
}

fn initImGui() !void {
    _ = c.ImGui_CreateContext(null);
    c.ImGui_StyleColorsDark(null);
    const io = c.ImGui_GetIO();
    io.*.ConfigFlags |= @as(c.ImGuiConfigFlags, @intCast(c.ImGuiConfigFlags_NavEnableKeyboard));

    if (!c.cImGui_ImplSDL3_InitForOpenGL(app_global.window, app_global.gl_context)) {
        return error.ImGuiInitFailed;
    }
    if (!c.cImGui_ImplOpenGL3_Init()) {
        return error.ImGuiInitFailed;
    }
}

fn shutdown(app: *App) void {
    if (app.audio_stream) |stream| {
        _ = c.SDL_PauseAudioStreamDevice(stream);
        c.SDL_DestroyAudioStream(stream);
    }
    if (app.ctx) |ctx| {
        c.blip_destroy(ctx);
    }
    c.cImGui_ImplOpenGL3_Shutdown();
    c.cImGui_ImplSDL3_Shutdown();
    c.ImGui_DestroyContext(null);
    if (app.gl_context != null) {
        _ = c.SDL_GL_DestroyContext(app.gl_context);
    }
    if (app.window) |window| {
        c.SDL_DestroyWindow(window);
    }
    c.SDL_Quit();
}

fn settingsEqual(a: *const c.BlipDemoSettings, b: *const c.BlipDemoSettings) bool {
    return std.mem.eql(u8, std.mem.asBytes(a), std.mem.asBytes(b));
}

fn applySettingsChange(app: *App) void {
    if (app.ctx) |ctx| {
        c.blip_reset(ctx);
    }
    if (app.audio_stream) |stream| {
        _ = c.SDL_ClearAudioStream(stream);
    }
    refreshPreview(app);
}

fn resetDemo(app: *App, demo_kind: c_int) void {
    app.current_demo = demo_kind;
    c.blip_get_default_settings(demo_kind, &app.settings);
    syncLegacyAxesFromSettings(&app.settings);
    applySettingsChange(app);
}

fn pumpAudio(app: *App) void {
    const stream = app.audio_stream orelse return;
    if (app.settings.play_audio == 0 or c.blip_demo_supports_audio(app.current_demo) == 0) {
        _ = c.SDL_ClearAudioStream(stream);
        return;
    }

    while (c.SDL_GetAudioStreamQueued(stream) < audio_frames * 8) {
        const frames = c.blip_render_audio(app.ctx, &app.settings, &app.stereo_buffer[0], audio_frames);
        if (frames <= 0) break;
        const bytes = frames * 2 * @as(c_int, @sizeOf(c_short));
        if (!c.SDL_PutAudioStreamData(stream, &app.stereo_buffer[0], bytes)) {
            break;
        }
    }
}

fn refreshPreview(app: *App) void {
    app.preview_len = c.blip_render_preview(app.ctx, &app.settings, &app.preview[0], preview_capacity);
}

fn imguiVec2(x: f32, y: f32) c.ImVec2 {
    return .{ .x = x, .y = y };
}

fn clamp01(value: f32) f32 {
    return std.math.clamp(value, 0.0, 1.0);
}

fn syncLegacyAxesFromSettings(settings: *c.BlipDemoSettings) void {
    switch (settings.demo_kind) {
        c.BLIP_DEMO_SQUARE, c.BLIP_DEMO_CONTINUOUS => {
            settings.mouse_x = clamp01((@as(f32, @floatFromInt(settings.period)) - 10.0) / 100.0);
            settings.mouse_y = clamp01((@as(f32, @floatFromInt(settings.amplitude)) - 1.0) / 9.0);
        },
        c.BLIP_DEMO_CLOCK_RATE => {
            const rate_ratio = @as(f32, @floatFromInt(settings.clock_rate)) / @as(f32, @floatFromInt(settings.sample_rate));
            settings.mouse_x = clamp01((rate_ratio - 1.0) / 10.0);
        },
        c.BLIP_DEMO_STEREO => {
            settings.mouse_x = clamp01((@as(f32, @floatFromInt(settings.period)) - 1000.0) / 6.0);
        },
        else => {},
    }
}

fn applyLegacyAxes(settings: *c.BlipDemoSettings) void {
    switch (settings.demo_kind) {
        c.BLIP_DEMO_SQUARE, c.BLIP_DEMO_CONTINUOUS => {
            settings.period = @as(c_int, @intFromFloat(std.math.round(10.0 + settings.mouse_x * 100.0)));
            settings.amplitude = @as(c_int, @intFromFloat(std.math.round(1.0 + settings.mouse_y * 9.0)));
        },
        c.BLIP_DEMO_CLOCK_RATE => {
            settings.clock_rate = @as(c_int, @intFromFloat(std.math.round(@as(f32, @floatFromInt(settings.sample_rate)) * (1.0 + settings.mouse_x * 10.0))));
        },
        c.BLIP_DEMO_STEREO => {
            settings.period = @as(c_int, @intFromFloat(std.math.round(1000.0 + settings.mouse_x * 6.0)));
        },
        else => {},
    }
}

fn rgba(r: u8, g: u8, b: u8, a: u8) c.ImU32 {
    return (@as(c.ImU32, a) << 24) | (@as(c.ImU32, b) << 16) | (@as(c.ImU32, g) << 8) | @as(c.ImU32, r);
}

fn drawDemoList(app: *App) bool {
    var changed = false;
    c.ImGui_SeparatorText("Demos");
    var idx: c_int = 0;
    while (idx < c.BLIP_DEMO_COUNT) : (idx += 1) {
        var label_buf: [128:0]u8 = undefined;
        const demo_name = std.mem.span(c.blip_demo_name(idx));
        const label = std.fmt.bufPrintZ(&label_buf, "{s}##demo_{d}", .{ demo_name, idx }) catch continue;
        var selected = app.current_demo;
        if (c.ImGui_RadioButtonIntPtr(label.ptr, &selected, idx)) {
            resetDemo(app, selected);
            changed = true;
        }
    }
    return changed;
}

fn drawControls(app: *App) bool {
    var changed = false;
    c.ImGui_SeparatorText("Controls");
    c.ImGui_TextUnformatted(c.blip_demo_summary(app.current_demo));

    switch (app.current_demo) {
        c.BLIP_DEMO_SQUARE, c.BLIP_DEMO_CONTINUOUS => {
            c.ImGui_SetNextItemWidth(240);
            if (c.ImGui_SliderFloat("Frequency##mouse_x", &app.settings.mouse_x, 0.0, 1.0)) {
                applyLegacyAxes(&app.settings);
                changed = true;
            }
            c.ImGui_SetNextItemWidth(240);
            if (c.ImGui_SliderFloat("Volume##mouse_y", &app.settings.mouse_y, 0.0, 1.0)) {
                applyLegacyAxes(&app.settings);
                changed = true;
            }
            c.ImGui_Text("Period: %d", app.settings.period);
            c.ImGui_Text("Amplitude: %d", app.settings.amplitude);
        },
        c.BLIP_DEMO_CLOCK_RATE => {
            c.ImGui_SetNextItemWidth(240);
            if (c.ImGui_SliderFloat("Clock Density##mouse_x", &app.settings.mouse_x, 0.0, 1.0)) {
                applyLegacyAxes(&app.settings);
                changed = true;
            }
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Amplitude##amplitude", &app.settings.amplitude, 1, 10) or changed;
            c.ImGui_Text("Clock Rate: %d Hz", app.settings.clock_rate);
        },
        c.BLIP_DEMO_STEREO => {
            c.ImGui_SetNextItemWidth(240);
            if (c.ImGui_SliderFloat("Left Pitch##mouse_x", &app.settings.mouse_x, 0.0, 1.0)) {
                applyLegacyAxes(&app.settings);
                changed = true;
            }
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Amplitude##amplitude", &app.settings.amplitude, 1, 10) or changed;
            c.ImGui_Text("Left Period: %d", app.settings.period);
            c.ImGui_TextUnformatted("Right channel stays at the original fixed pitch.");
        },
        c.BLIP_DEMO_TREBLE_BASS => {
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Amplitude##amplitude", &app.settings.amplitude, 1, 10) or changed;
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderFloat("Treble dB##treble_db", &app.settings.treble_db, -60.0, 8.0) or changed;
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Bass Hz##bass_hz", &app.settings.bass_freq, 0, 10000) or changed;
        },
        c.BLIP_DEMO_BUFFERING => {
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Period##period", &app.settings.period, 4, 1500) or changed;
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Amplitude##amplitude", &app.settings.amplitude, 1, 10) or changed;
        },
        else => {
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Period##period", &app.settings.period, 4, 1500) or changed;
            c.ImGui_SetNextItemWidth(240);
            changed = c.ImGui_SliderInt("Amplitude##amplitude", &app.settings.amplitude, 1, 10) or changed;
        },
    }

    if (app.current_demo == c.BLIP_DEMO_BUFFERING) {
        c.ImGui_SeparatorText("Buffering");
        changed = c.ImGui_RadioButtonIntPtr("Immediate##buffer_immediate", &app.settings.buffering_mode, c.BLIP_BUFFER_IMMEDIATE) or changed;
        changed = c.ImGui_RadioButtonIntPtr("Accumulate##buffer_accumulate", &app.settings.buffering_mode, c.BLIP_BUFFER_ACCUMULATE) or changed;
        changed = c.ImGui_RadioButtonIntPtr("On Demand##buffer_on_demand", &app.settings.buffering_mode, c.BLIP_BUFFER_ON_DEMAND) or changed;
    }

    var play_audio = app.settings.play_audio != 0;
    if (c.blip_demo_supports_audio(app.current_demo) == 0) {
        c.ImGui_BeginDisabled(true);
        _ = c.ImGui_Checkbox("Play Audio##play_audio", &play_audio);
        c.ImGui_EndDisabled();
        play_audio = false;
    } else {
        changed = c.ImGui_Checkbox("Play Audio##play_audio", &play_audio) or changed;
    }
    app.settings.play_audio = if (play_audio) 1 else 0;

    if (c.ImGui_Button("Reset Demo##reset_demo")) {
        resetDemo(app, app.current_demo);
        changed = true;
    }
    return changed;
}

fn drawWaveBackdrop(app: *App) void {
    if (app.preview_len < 2) {
        return;
    }

    const draw_list = c.ImGui_GetWindowDrawList();
    const pos = c.ImGui_GetWindowPos();
    const size = c.ImGui_GetWindowSize();
    const left = pos.x + 28.0;
    const right = pos.x + size.x - 28.0;
    const top = pos.y + 32.0;
    const bottom = pos.y + size.y - 32.0;
    const width = right - left;
    const height = bottom - top;
    if (width <= 0.0 or height <= 0.0) {
        return;
    }

    const center_y = top + height * 0.5;
    const color_primary = rgba(110, 220, 255, 60);
    const color_secondary = rgba(255, 180, 120, 28);
    const color_mid = rgba(255, 255, 255, 18);

    c.ImDrawList_AddLine(draw_list, imguiVec2(left, center_y), imguiVec2(right, center_y), color_mid);

    var index: usize = 1;
    while (index < @as(usize, @intCast(app.preview_len))) : (index += 1) {
        const x0 = left + width * (@as(f32, @floatFromInt(index - 1)) / @as(f32, @floatFromInt(app.preview_len - 1)));
        const x1 = left + width * (@as(f32, @floatFromInt(index)) / @as(f32, @floatFromInt(app.preview_len - 1)));
        const y0 = center_y - app.preview[index - 1] * (height * 0.22);
        const y1 = center_y - app.preview[index] * (height * 0.22);
        const y0_echo = center_y - app.preview[index - 1] * (height * 0.36);
        const y1_echo = center_y - app.preview[index] * (height * 0.36);
        c.ImDrawList_AddLine(draw_list, imguiVec2(x0, y0_echo), imguiVec2(x1, y1_echo), color_secondary);
        c.ImDrawList_AddLine(draw_list, imguiVec2(x0, y0), imguiVec2(x1, y1), color_primary);
    }
}

fn renderUi(app: *App) bool {
    var win_w: c_int = 0;
    var win_h: c_int = 0;
    _ = c.SDL_GetWindowSize(app.window, &win_w, &win_h);

    c.ImGui_SetNextWindowPos(imguiVec2(0.0, 0.0), c.ImGuiCond_Always);
    c.ImGui_SetNextWindowSize(imguiVec2(@floatFromInt(win_w), @floatFromInt(win_h)), c.ImGuiCond_Always);
    c.ImGui_SetNextWindowBgAlpha(ui_bg_alpha);

    const window_flags = c.ImGuiWindowFlags_NoTitleBar |
        c.ImGuiWindowFlags_NoResize |
        c.ImGuiWindowFlags_NoMove |
        c.ImGuiWindowFlags_NoCollapse |
        c.ImGuiWindowFlags_NoSavedSettings;

    _ = c.ImGui_Begin("Blip Buffer Demo Lab##root", null, window_flags);
    drawWaveBackdrop(app);
    const list_changed = drawDemoList(app);
    const controls_changed = drawControls(app);
    c.ImGui_End();
    return list_changed or controls_changed;
}

pub fn main() !void {
    app_global.ctx = c.blip_create();
    if (app_global.ctx == null) return error.OutOfMemory;

    resetDemo(&app_global, c.BLIP_DEMO_WAVEFORM);
    try initWindow(&app_global);
    errdefer shutdown(&app_global);
    try initAudio(&app_global);
    try initImGui();
    refreshPreview(&app_global);

    while (app_global.running) {
        var event: c.SDL_Event = undefined;
        while (c.SDL_PollEvent(&event)) {
            _ = c.cImGui_ImplSDL3_ProcessEvent(&event);
            switch (event.type) {
                c.SDL_EVENT_QUIT, c.SDL_EVENT_WINDOW_CLOSE_REQUESTED => app_global.running = false,
                else => {},
            }
        }

        pumpAudio(&app_global);

        c.cImGui_ImplOpenGL3_NewFrame();
        c.cImGui_ImplSDL3_NewFrame();
        c.ImGui_NewFrame();

        const before_settings = app_global.settings;
        const ui_changed = renderUi(&app_global);
        if (ui_changed or !settingsEqual(&before_settings, &app_global.settings)) {
            applySettingsChange(&app_global);
        }

        c.ImGui_Render();
        _ = c.glViewport(0, 0, 1280, 800);
        _ = c.glClearColor(0.08, 0.09, 0.12, 1.0);
        _ = c.glClear(c.GL_COLOR_BUFFER_BIT);
        c.cImGui_ImplOpenGL3_RenderDrawData(c.ImGui_GetDrawData());
        _ = c.SDL_GL_SwapWindow(app_global.window);
    }

    shutdown(&app_global);
}
