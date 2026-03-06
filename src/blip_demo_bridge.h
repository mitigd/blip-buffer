#ifndef BLIP_DEMO_BRIDGE_H
#define BLIP_DEMO_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

enum BlipDemoKind {
    BLIP_DEMO_WAVEFORM = 0,
    BLIP_DEMO_SQUARE,
    BLIP_DEMO_CLOCK_RATE,
    BLIP_DEMO_CONTINUOUS,
    BLIP_DEMO_MULTIPLE_WAVES,
    BLIP_DEMO_STEREO,
    BLIP_DEMO_TREBLE_BASS,
    BLIP_DEMO_BUFFERING,
    BLIP_DEMO_EXTERNAL_MIXING,
    BLIP_DEMO_DELTA_SYNTH,
    BLIP_DEMO_COUNT,
};

enum BlipBufferingMode {
    BLIP_BUFFER_IMMEDIATE = 0,
    BLIP_BUFFER_ACCUMULATE,
    BLIP_BUFFER_ON_DEMAND,
};

typedef struct BlipDemoSettings {
    int demo_kind;
    int sample_rate;
    float mouse_x;
    float mouse_y;
    int period;
    int amplitude;
    int clock_rate;
    int frame_clocks;
    float treble_db;
    int bass_freq;
    int buffering_mode;
    int play_audio;
} BlipDemoSettings;

typedef struct BlipContext BlipContext;

BlipContext* blip_create(void);
void blip_destroy(BlipContext* ctx);
void blip_reset(BlipContext* ctx);
void blip_get_default_settings(int demo_kind, BlipDemoSettings* out_settings);
const char* blip_demo_name(int demo_kind);
const char* blip_demo_summary(int demo_kind);
int blip_demo_supports_audio(int demo_kind);
int blip_render_preview(BlipContext* ctx, const BlipDemoSettings* settings, float* out_samples, int max_samples);
int blip_render_audio(BlipContext* ctx, const BlipDemoSettings* settings, short* out_stereo_samples, int max_frames);

#ifdef __cplusplus
}
#endif

#endif