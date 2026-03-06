#include "blip_demo_bridge.h"

#include "Blip_Buffer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

struct BlipContext {
        Blip_Buffer mono;
        Blip_Buffer left;
        Blip_Buffer right;
    Blip_Synth<blip_low_quality, 20> low_synth_a;
    Blip_Synth<blip_low_quality, 20> low_synth_b;
    Blip_Synth<blip_good_quality, 20> good_synth_a;
    Blip_Synth<blip_good_quality, 20> good_synth_b;
    Blip_Synth<blip_high_quality, 20> high_synth_a;
    Blip_Synth<blip_good_quality, 20> left_synth;
    Blip_Synth<blip_good_quality, 20> right_synth;
        BlipDemoSettings last_settings;
        bool have_settings;
        int mono_time;
        int mono_sign;
        int mono_amp;
        int left_time;
        int left_amp;
        int right_time;
        int right_amp;

        BlipContext()
                : have_settings(false),
                    mono_time(0),
                    mono_sign(1),
                    mono_amp(5),
                    left_time(0),
                    left_amp(0),
                    right_time(0),
                    right_amp(0) {
                std::memset(&last_settings, 0, sizeof(last_settings));
        }
};

namespace {

constexpr long k_default_sample_rate = 44100;
constexpr int k_preview_clocks = 1000;
constexpr int k_audio_chunk_clocks = 1000;
constexpr int k_max_temp_samples = 8192;

using LowSynth = Blip_Synth<blip_low_quality, 20>;
using GoodSynth = Blip_Synth<blip_good_quality, 20>;
using HighSynth = Blip_Synth<blip_high_quality, 20>;

struct DemoInfo {
    const char* name;
    const char* summary;
    bool supports_audio;
};

constexpr DemoInfo k_demo_info[] = {
    {"Waveform", "Static step waveform from the first tutorial demo.", true},
    {"Square", "Interactive square wave driven by the original mouse X/Y idea.", true},
    {"Clock Rate", "Shows how the source clock rate changes waveform density.", true},
    {"Continuous", "Streams a continuous square wave and keeps phase between frames.", true},
    {"Multiple Waves", "Mixes two square waves into the same Blip_Buffer.", true},
    {"Stereo", "Generates left/right waveforms with slightly different pitch.", true},
    {"Treble + Bass", "Applies Blip_Buffer bass and Blip_Synth treble shaping.", true},
    {"Buffering", "Demonstrates immediate, accumulated, and on-demand buffering.", true},
    {"External Mixing", "Mixes an external sine wave into the generated square wave.", true},
    {"Delta Synth", "Uses the low-level delta API instead of amplitude tracking.", true},
};

int clampi(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

int scaled_level(int level, int amplitude) {
    return static_cast<int>(std::lround(level * (static_cast<double>(clampi(amplitude, 1, 10)) / 10.0)));
}

int scaled_time(int base_time, int period_setting, int base_period) {
    const int period = clampi(period_setting, 4, 2000);
    return std::max(1, static_cast<int>(std::lround(base_time * (static_cast<double>(period) / base_period))));
}

void setup_buffer(Blip_Buffer& buf, long sample_rate, long clock_rate, int bass_freq) {
    buf.set_sample_rate(sample_rate, 1000);
    buf.clock_rate(clock_rate);
    buf.bass_freq(bass_freq);
    buf.clear();
}

template <typename Synth>
void setup_synth(Synth& synth, Blip_Buffer& buf, float volume, float treble_db) {
    synth.output(&buf);
    synth.volume(volume);
    synth.treble_eq(treble_db);
}

int read_preview_samples(Blip_Buffer& buf, float* out_samples, int max_samples) {
    blip_sample_t temp[k_max_temp_samples];
    const int count = static_cast<int>(buf.read_samples(temp, std::min(max_samples, k_max_temp_samples)));
    for (int i = 0; i < count; ++i) {
        out_samples[i] = static_cast<float>(temp[i]) / static_cast<float>(blip_sample_max);
    }
    return count;
}

int fill_waveform_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer buf;
    LowSynth synth;
    setup_buffer(buf, settings.sample_rate, settings.sample_rate, 0);
    setup_synth(synth, buf, 0.50f, 0.0f);

    synth.update(scaled_time(100, settings.period, 100), scaled_level(5, settings.amplitude));
    synth.update(scaled_time(200, settings.period, 100), 0);
    synth.update(scaled_time(300, settings.period, 100), scaled_level(-10, settings.amplitude));
    synth.update(scaled_time(400, settings.period, 100), scaled_level(10, settings.amplitude));
    synth.update(scaled_time(500, settings.period, 100), 0);
    buf.end_frame(scaled_time(700, settings.period, 100));
    return read_preview_samples(buf, out_samples, max_samples);
}

int fill_square_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer buf;
    if (settings.demo_kind == BLIP_DEMO_CONTINUOUS) {
        GoodSynth synth;
        setup_buffer(buf, settings.sample_rate, settings.sample_rate, 0);
        setup_synth(synth, buf, 0.50f, 0.0f);

        const int period = clampi(settings.period, 4, 1000);
        int amplitude = clampi(settings.amplitude, 1, 10);
        for (int time = 0; time < k_preview_clocks; time += period) {
            amplitude = -amplitude;
            synth.update(time, amplitude);
        }
        buf.end_frame(k_preview_clocks);
        return read_preview_samples(buf, out_samples, max_samples);
    }

    LowSynth synth;
    setup_buffer(buf, settings.sample_rate, settings.sample_rate, 0);
    setup_synth(synth, buf, 0.50f, 0.0f);

    const int period = clampi(settings.period, 4, 1000);
    int amplitude = clampi(settings.amplitude, 1, 10);
    for (int time = 0; time < k_preview_clocks; time += period) {
        amplitude = -amplitude;
        synth.update(time, amplitude);
    }
    buf.end_frame(k_preview_clocks);
    return read_preview_samples(buf, out_samples, max_samples);
}

int fill_clock_rate_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer buf;
    LowSynth synth;
    setup_buffer(buf, settings.sample_rate, settings.clock_rate, 0);
    setup_synth(synth, buf, 0.50f, 0.0f);

    std::srand(1);
    for (int time = 0; time < 500; time += 50) {
        synth.update(time, scaled_level(std::rand() % 20 - 10, settings.amplitude));
    }
    buf.end_frame(600);
    return read_preview_samples(buf, out_samples, max_samples);
}

int fill_multiple_waves_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer buf;
    GoodSynth synth_a;
    GoodSynth synth_b;
    setup_buffer(buf, settings.sample_rate, settings.sample_rate, 0);
    setup_synth(synth_a, buf, 0.50f, 0.0f);
    setup_synth(synth_b, buf, 0.50f, 0.0f);

    const int low_period = scaled_time(100, settings.period, 100);
    const int high_period = scaled_time(15, settings.period, 100);
    const int length = scaled_time(1000, settings.period, 100);
    int amplitude = scaled_level(5, settings.amplitude);
    for (int time = 0; time < length; time += low_period) {
        synth_a.update(time, amplitude);
        amplitude = -amplitude;
    }
    amplitude = scaled_level(2, settings.amplitude);
    for (int time = 0; time < length; time += high_period) {
        synth_b.update(time, amplitude);
        amplitude = -amplitude;
    }
    buf.end_frame(length);
    return read_preview_samples(buf, out_samples, max_samples);
}

int fill_treble_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer buf;
    HighSynth synth;
    setup_buffer(buf, settings.sample_rate, settings.sample_rate, clampi(settings.bass_freq, 0, 22000));
    setup_synth(synth, buf, 0.50f, settings.treble_db);

    const int amplitude = scaled_level(10, settings.amplitude);
    for (int i = 0; i < 10; ++i) {
        synth.update(i * 100, (i & 1) ? amplitude : -amplitude);
    }
    buf.end_frame(1000);
    return read_preview_samples(buf, out_samples, max_samples);
}

int fill_external_mix_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer buf;
    GoodSynth synth;
    setup_buffer(buf, settings.sample_rate, settings.sample_rate, 0);
    setup_synth(synth, buf, 0.50f, 0.0f);

    const int period = scaled_time(10, settings.period, 10);
    const int length = 1000;
    int amplitude = scaled_level(1, settings.amplitude);
    for (int time = 0; time < length; time += period) {
        synth.update(time, amplitude);
        amplitude = -amplitude;
    }

    buf.end_frame(length);

    blip_sample_t generated[k_max_temp_samples];
    const int count = static_cast<int>(buf.read_samples(generated, std::min(max_samples, k_max_temp_samples)));
    for (int i = 0; i < count; ++i) {
        const double y = std::sin(i * (3.14159265358979323846 / 100.0));
        const long sine = static_cast<long>(std::lround(y * 0.30 * blip_sample_max));
        const long mixed = static_cast<long>(generated[i]) + sine;
        const long clamped = std::max<long>(-blip_sample_max, std::min<long>(blip_sample_max, mixed));
        out_samples[i] = static_cast<float>(clamped) / static_cast<float>(blip_sample_max);
    }
    return count;
}

int fill_delta_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer buf;
    LowSynth synth;
    setup_buffer(buf, settings.sample_rate, settings.sample_rate, 0);
    setup_synth(synth, buf, 0.50f, 0.0f);

    synth.offset(scaled_time(100, settings.period, 100), scaled_level(5, settings.amplitude));
    synth.offset(scaled_time(200, settings.period, 100), scaled_level(-5, settings.amplitude));
    synth.offset(scaled_time(300, settings.period, 100), scaled_level(-10, settings.amplitude));
    synth.offset(scaled_time(400, settings.period, 100), scaled_level(20, settings.amplitude));
    synth.offset(scaled_time(500, settings.period, 100), scaled_level(-10, settings.amplitude));
    buf.end_frame(scaled_time(700, settings.period, 100));
    return read_preview_samples(buf, out_samples, max_samples);
}

int fill_stereo_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    Blip_Buffer left;
    Blip_Buffer right;
    GoodSynth left_synth;
    GoodSynth right_synth;
    setup_buffer(left, settings.sample_rate, settings.sample_rate * 100, 0);
    setup_buffer(right, settings.sample_rate, settings.sample_rate * 100, 0);
    setup_synth(left_synth, left, 0.50f, 0.0f);
    setup_synth(right_synth, right, 0.50f, 0.0f);

    int left_amp = 0;
    int right_amp = 0;
    const int period = clampi(settings.period, 4, 2000);
    for (int time = 0; time < 100000; time += period) {
        left_amp = (left_amp + 1) % 10;
        left_synth.update(time, scaled_level(left_amp, settings.amplitude));
    }
    for (int time = 0; time < 100000; time += 1000) {
        right_amp = (right_amp + 1) % 10;
        right_synth.update(time, scaled_level(right_amp, settings.amplitude));
    }
    left.end_frame(100000);
    right.end_frame(100000);

    blip_sample_t left_buf[k_max_temp_samples];
    blip_sample_t right_buf[k_max_temp_samples];
    const int count = static_cast<int>(left.read_samples(left_buf, std::min(max_samples, k_max_temp_samples)));
    right.read_samples(right_buf, count);
    for (int i = 0; i < count; ++i) {
        out_samples[i] = static_cast<float>(left_buf[i] + right_buf[i]) / (2.0f * blip_sample_max);
    }
    return count;
}

int fill_preview(const BlipDemoSettings& settings, float* out_samples, int max_samples) {
    switch (settings.demo_kind) {
        case BLIP_DEMO_WAVEFORM:
            return fill_waveform_preview(settings, out_samples, max_samples);
        case BLIP_DEMO_SQUARE:
        case BLIP_DEMO_CONTINUOUS:
        case BLIP_DEMO_BUFFERING:
            return fill_square_preview(settings, out_samples, max_samples);
        case BLIP_DEMO_CLOCK_RATE:
            return fill_clock_rate_preview(settings, out_samples, max_samples);
        case BLIP_DEMO_MULTIPLE_WAVES:
            return fill_multiple_waves_preview(settings, out_samples, max_samples);
        case BLIP_DEMO_STEREO:
            return fill_stereo_preview(settings, out_samples, max_samples);
        case BLIP_DEMO_TREBLE_BASS:
            return fill_treble_preview(settings, out_samples, max_samples);
        case BLIP_DEMO_EXTERNAL_MIXING:
            return fill_external_mix_preview(settings, out_samples, max_samples);
        case BLIP_DEMO_DELTA_SYNTH:
            return fill_delta_preview(settings, out_samples, max_samples);
        default:
            return 0;
    }
}

void copy_mono_to_stereo(const blip_sample_t* in, int frames, short* out_stereo) {
    for (int i = 0; i < frames; ++i) {
        out_stereo[i * 2 + 0] = in[i];
        out_stereo[i * 2 + 1] = in[i];
    }
}

int render_preview_loop_audio(const BlipDemoSettings& settings, short* out_stereo_samples, int max_frames) {
    float preview[k_max_temp_samples];
    const int preview_count = fill_preview(settings, preview, std::min(max_frames, k_max_temp_samples));
    if (preview_count <= 0) {
        return 0;
    }

    for (int i = 0; i < max_frames; ++i) {
        const float sample = preview[i % preview_count];
        const float scaled = sample * static_cast<float>(blip_sample_max);
        const long quantized = static_cast<long>(std::lround(scaled));
        const blip_sample_t clipped = static_cast<blip_sample_t>(
            std::max<long>(-blip_sample_max, std::min<long>(blip_sample_max, quantized))
        );
        out_stereo_samples[i * 2 + 0] = clipped;
        out_stereo_samples[i * 2 + 1] = clipped;
    }
    return max_frames;
}

void reset_runtime(BlipContext* ctx, const BlipDemoSettings& settings) {
    ctx->mono.clear();
    ctx->left.clear();
    ctx->right.clear();

    ctx->mono.set_sample_rate(settings.sample_rate, 1000);
    ctx->mono.clock_rate(settings.clock_rate > 0 ? settings.clock_rate : settings.sample_rate);
    ctx->mono.bass_freq(clampi(settings.bass_freq, 0, 22000));
    setup_synth(ctx->low_synth_a, ctx->mono, 0.50f, settings.treble_db);
    setup_synth(ctx->low_synth_b, ctx->mono, 0.50f, settings.treble_db);
    setup_synth(ctx->good_synth_a, ctx->mono, 0.50f, settings.treble_db);
    setup_synth(ctx->good_synth_b, ctx->mono, 0.50f, settings.treble_db);
    setup_synth(ctx->high_synth_a, ctx->mono, 0.50f, settings.treble_db);

    ctx->left.set_sample_rate(settings.sample_rate, 1000);
    ctx->left.clock_rate(settings.sample_rate * 100);
    ctx->left.bass_freq(0);
    ctx->right.set_sample_rate(settings.sample_rate, 1000);
    ctx->right.clock_rate(settings.sample_rate * 100);
    ctx->right.bass_freq(0);
    ctx->left_synth.output(&ctx->left);
    ctx->right_synth.output(&ctx->right);
    ctx->left_synth.volume(0.50);
    ctx->right_synth.volume(0.50);

    ctx->mono_time = 0;
    ctx->mono_sign = 1;
    ctx->mono_amp = clampi(settings.amplitude, 1, 10);
    ctx->left_time = 0;
    ctx->left_amp = 0;
    ctx->right_time = 0;
    ctx->right_amp = 0;
    ctx->last_settings = settings;
    ctx->have_settings = true;
}

bool settings_changed(const BlipContext* ctx, const BlipDemoSettings& settings) {
    return !ctx->have_settings || std::memcmp(&ctx->last_settings, &settings, sizeof(settings)) != 0;
}

void generate_square_chunk(BlipContext* ctx, const BlipDemoSettings& settings) {
    const int period = clampi(settings.period, 4, 2000);
    const int volume = clampi(settings.amplitude, 1, 10);
    while (ctx->mono_time < k_audio_chunk_clocks) {
        ctx->mono_sign = -ctx->mono_sign;
        if (settings.demo_kind == BLIP_DEMO_CONTINUOUS) {
            ctx->good_synth_a.update(ctx->mono_time, ctx->mono_sign * volume);
        } else {
            ctx->low_synth_a.update(ctx->mono_time, ctx->mono_sign * volume);
        }
        ctx->mono_time += period;
    }
    ctx->mono.end_frame(k_audio_chunk_clocks);
    ctx->mono_time -= k_audio_chunk_clocks;
}

void generate_treble_chunk(BlipContext* ctx, const BlipDemoSettings& settings) {
    const int amplitude = scaled_level(10, settings.amplitude);
    for (int i = 0; i < 10; ++i) {
        ctx->high_synth_a.update(i * 100, (i & 1) ? amplitude : -amplitude);
    }
    ctx->mono.end_frame(1000);
}

void generate_stereo_chunk(BlipContext* ctx, const BlipDemoSettings& settings) {
    const int period = clampi(settings.period, 10, 2000);
    const blip_time_t length = 100000;
    do {
        ctx->left_amp = (ctx->left_amp + 1) % 10;
        ctx->left_synth.update(ctx->left_time, scaled_level(ctx->left_amp, settings.amplitude));
    } while ((ctx->left_time += period) < length);
    ctx->left.end_frame(length);
    ctx->left_time -= static_cast<int>(length);

    do {
        ctx->right_amp = (ctx->right_amp + 1) % 10;
        ctx->right_synth.update(ctx->right_time, scaled_level(ctx->right_amp, settings.amplitude));
    } while ((ctx->right_time += 1000) < length);
    ctx->right.end_frame(length);
    ctx->right_time -= static_cast<int>(length);
}

int render_mono_audio(BlipContext* ctx, const BlipDemoSettings& settings, short* out_stereo_samples, int max_frames) {
    while (ctx->mono.samples_avail() < max_frames) {
        switch (settings.demo_kind) {
            case BLIP_DEMO_SQUARE:
            case BLIP_DEMO_CONTINUOUS:
                generate_square_chunk(ctx, settings);
                break;
            case BLIP_DEMO_TREBLE_BASS:
                generate_treble_chunk(ctx, settings);
                break;
            case BLIP_DEMO_BUFFERING:
                if (settings.buffering_mode == BLIP_BUFFER_ON_DEMAND) {
                    const int samples_needed = std::max(256, max_frames - static_cast<int>(ctx->mono.samples_avail()));
                    const int length = static_cast<int>(ctx->mono.count_clocks(samples_needed));
                    const int period = clampi(settings.period, 4, 2000);
                    int time = 0;
                    while (time < length) {
                        ctx->mono_sign = -ctx->mono_sign;
                        ctx->low_synth_a.update(time, ctx->mono_sign * clampi(settings.amplitude, 1, 10));
                        time += period;
                    }
                    ctx->mono.end_frame(length);
                } else if (settings.buffering_mode == BLIP_BUFFER_ACCUMULATE) {
                    while (ctx->mono.samples_avail() < 4000) {
                        generate_square_chunk(ctx, settings);
                    }
                } else {
                    generate_square_chunk(ctx, settings);
                }
                break;
            default:
                return 0;
        }
    }

    blip_sample_t mono_samples[k_max_temp_samples];
    const int frames = static_cast<int>(ctx->mono.read_samples(mono_samples, std::min(max_frames, k_max_temp_samples)));
    copy_mono_to_stereo(mono_samples, frames, out_stereo_samples);
    return frames;
}

} // namespace

BlipContext* blip_create(void) {
    return new (std::nothrow) BlipContext();
}

void blip_destroy(BlipContext* ctx) {
    delete ctx;
}

void blip_reset(BlipContext* ctx) {
    if (!ctx) {
        return;
    }
    ctx->have_settings = false;
    ctx->mono.clear();
    ctx->left.clear();
    ctx->right.clear();
}

void blip_get_default_settings(int demo_kind, BlipDemoSettings* out_settings) {
    if (!out_settings) {
        return;
    }

    std::memset(out_settings, 0, sizeof(*out_settings));
    out_settings->demo_kind = clampi(demo_kind, 0, BLIP_DEMO_COUNT - 1);
    out_settings->sample_rate = k_default_sample_rate;
    out_settings->mouse_x = 0.40f;
    out_settings->mouse_y = 0.55f;
    out_settings->period = 100;
    out_settings->amplitude = 10;
    out_settings->clock_rate = k_default_sample_rate;
    out_settings->frame_clocks = k_audio_chunk_clocks;
    out_settings->treble_db = -8.0f;
    out_settings->bass_freq = 16;
    out_settings->buffering_mode = BLIP_BUFFER_IMMEDIATE;
    out_settings->play_audio = k_demo_info[out_settings->demo_kind].supports_audio ? 1 : 0;

    switch (out_settings->demo_kind) {
        case BLIP_DEMO_SQUARE:
        case BLIP_DEMO_CONTINUOUS:
            out_settings->period = 48;
            out_settings->amplitude = 6;
            break;
        case BLIP_DEMO_CLOCK_RATE:
            out_settings->clock_rate = k_default_sample_rate * 4;
            break;
        case BLIP_DEMO_STEREO:
            out_settings->period = 1006;
            out_settings->frame_clocks = 100000;
            break;
        case BLIP_DEMO_TREBLE_BASS:
            out_settings->amplitude = 10;
            out_settings->treble_db = -12.0f;
            out_settings->bass_freq = 180;
            break;
        case BLIP_DEMO_BUFFERING:
            out_settings->period = 50;
            out_settings->amplitude = 5;
            break;
        case BLIP_DEMO_EXTERNAL_MIXING:
            out_settings->period = 10;
            break;
        default:
            break;
    }
}

const char* blip_demo_name(int demo_kind) {
    if (demo_kind < 0 || demo_kind >= BLIP_DEMO_COUNT) {
        return "Unknown";
    }
    return k_demo_info[demo_kind].name;
}

const char* blip_demo_summary(int demo_kind) {
    if (demo_kind < 0 || demo_kind >= BLIP_DEMO_COUNT) {
        return "Unknown demo.";
    }
    return k_demo_info[demo_kind].summary;
}

int blip_demo_supports_audio(int demo_kind) {
    if (demo_kind < 0 || demo_kind >= BLIP_DEMO_COUNT) {
        return 0;
    }
    return k_demo_info[demo_kind].supports_audio ? 1 : 0;
}

int blip_render_preview(BlipContext* ctx, const BlipDemoSettings* settings, float* out_samples, int max_samples) {
    (void) ctx;
    if (!settings || !out_samples || max_samples <= 0) {
        return 0;
    }
    return fill_preview(*settings, out_samples, max_samples);
}

int blip_render_audio(BlipContext* ctx, const BlipDemoSettings* settings, short* out_stereo_samples, int max_frames) {
    if (!ctx || !settings || !out_stereo_samples || max_frames <= 0) {
        return 0;
    }
    if (!blip_demo_supports_audio(settings->demo_kind) || !settings->play_audio) {
        return 0;
    }

    if (settings_changed(ctx, *settings)) {
        reset_runtime(ctx, *settings);
    }

    switch (settings->demo_kind) {
        case BLIP_DEMO_SQUARE:
        case BLIP_DEMO_CONTINUOUS:
        case BLIP_DEMO_TREBLE_BASS:
        case BLIP_DEMO_BUFFERING:
            return render_mono_audio(ctx, *settings, out_stereo_samples, max_frames);

        case BLIP_DEMO_STEREO: {
            while (ctx->left.samples_avail() < max_frames || ctx->right.samples_avail() < max_frames) {
                generate_stereo_chunk(ctx, *settings);
            }
            const int frames = static_cast<int>(ctx->left.read_samples(out_stereo_samples, max_frames, 1));
            ctx->right.read_samples(out_stereo_samples + 1, frames, 1);
            return frames;
        }

        case BLIP_DEMO_WAVEFORM:
        case BLIP_DEMO_CLOCK_RATE:
        case BLIP_DEMO_MULTIPLE_WAVES:
        case BLIP_DEMO_EXTERNAL_MIXING:
        case BLIP_DEMO_DELTA_SYNTH:
            return render_preview_loop_audio(*settings, out_stereo_samples, max_frames);

        default:
            return 0;
    }
}