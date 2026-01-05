#pragma once

#include <stdint.h>
#include <stddef.h>

// Video is 16 bit pixels
#define VIDEO_WIDTH 1024
#define VIDEO_HEIGHT 512
// Used by core to log to ui. Frontends are expected to define this.


enum Keys {
    BTN_A = 0,
    BTN_B,
    BTN_Sel,
    BTN_Start,
    BTN_Up,
    BTN_Down,
    BTN_Left,
    BTN_Right,
    BTN_L,  // Shoulder buttons
    BTN_R,
    BTN_L2,
    BTN_R2,
    BTN_X,
    BTN_Y,
    NUM_KEYS
};


#define EXPOSE __attribute__((visibility("default")))

void corelib_set_puts(void(*cb)(const char*));

void set_key(size_t key, char val);
void init(const uint8_t* data, size_t len);
void copy_bios(const uint8_t* buffer, size_t len);
const uint8_t *framebuffer();
void frame();
// dynamic video
int framerate();
int width();
int height();

void dump_state(const char* save_path);
void load_state(const char* save_path);
// Interface used by app. App closes fd.
void save(int fd);
void load(int fd);
// file-free save interface
int save_str(uint8_t* dest, int capacity);
void load_str(int len, const uint8_t* src);


// APU
const int SAMPLE_RATE = 44100;
const int SAMPLES_PER_FRAME = SAMPLE_RATE / 60;
void apu_tick_60hz();
void apu_sample_60hz(int16_t *output);
long apu_sample_variable(int16_t *output, int32_t frames);
