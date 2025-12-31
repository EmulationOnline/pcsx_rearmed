#include "corelib.h"
#include "libpcsxcore/misc.h"
#include "libpcsxcore/psxcounters.h"
#include "libpcsxcore/psxmem_map.h"
#include "libpcsxcore/new_dynarec/new_dynarec.h"
#include "libpcsxcore/cdrom.h"
#include "libpcsxcore/cdrom-async.h"
#include "libpcsxcore/cdriso.h"
#include "libpcsxcore/cheat.h"
#include "libpcsxcore/r3000a.h"
#include "libpcsxcore/gpu.h"
#include "libpcsxcore/database.h"
#include "plugins/dfsound/out.h"
#include "plugins/dfsound/spu_config.h"
#include "frontend/cspace.h"
#include "frontend/main.h"
#include "ring.h"
// #include "menu.h"
// #include "plugin.h"
// #include "plugin_lib.h"
// #include "arm_features.h"
// #include "revision.h"


#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include "frontend/plugin_lib.h"
#include "frontend/plugin.h"


#define REQUIRE_CORE(val) if (!psxCpu) { puts(__func__); return val; }

struct ring_i16 ring_;
uint32_t fbuffer_[VIDEO_WIDTH * VIDEO_HEIGHT];
static int vout_w = VIDEO_WIDTH;
static int vout_h = VIDEO_HEIGHT;

// Video output
static int vout_open(void) { return 0; }
static void vout_close(void) {}

static void vout_set_mode(int w, int h, int raw_w, int raw_h, int bpp) {
    (void)raw_w; (void)raw_h; (void)bpp;
    vout_w = w;
    vout_h = h;
}

static void vout_flip(const void *vram, int vram_offset, int bgr24,
                      int x, int y, int w, int h, int dims_changed) {
    (void)dims_changed;

    if (vram == NULL)
        return;

    vram_offset = 0;  // prevent y bounce from interlace.
    const uint8_t *src = (const uint8_t *)vram + vram_offset;

    // Clamp to our buffer size
    int copy_w = (w < VIDEO_WIDTH) ? w : VIDEO_WIDTH;
    int copy_h = (h < VIDEO_HEIGHT) ? h : VIDEO_HEIGHT;
    // printf("copying (%d , %d)\n", copy_w, copy_h);

    if (bgr24) {
        // 24-bit BGR888: 3 bytes per pixel
        // Convert row by row since source stride may differ
        // void bgr888_to_xrgb8888(void *dst, const void *src, int bytes);
        int src_stride = w * 3 * 2;  // stridex2 to compensate for interleaved src video.
        for (int row = 0; row < copy_h; row++) {
            bgr888_to_xrgb8888(
                fbuffer_ + row * VIDEO_WIDTH,
                src + row * src_stride,
                copy_w * 3
            );
        }
    } else {
        // 16-bit BGR555: 2 bytes per pixel
        int src_stride = w * 2 * 2;
        for (int row = 0; row < copy_h; row++) {
            bgr555_to_xrgb8888(
                fbuffer_ + row * VIDEO_WIDTH,
                src + row * src_stride,
                copy_w * 2
            );
        }
    }
}

// Memory mapping for GPU VRAM
static void *pl_mmap(unsigned int size) {
    return psxMap(0, size, 0, MAP_TAG_VRAM);
}
static void pl_munmap(void *ptr, unsigned int size) {
    psxUnmap(ptr, size, MAP_TAG_VRAM);
}

void SysPrintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

// Input globals expected by some parts of the core/plugins
int in_type[8];
uint16_t in_keystate[8];
int in_analog_left[8][2];
int in_analog_right[8][2];
int in_mouse[8][2];
int multitap1;
int multitap2;

// Other expected globals
struct rearmed_cbs pl_rearmed_cbs = {
    .pl_vout_open     = vout_open,
    .pl_vout_set_mode = vout_set_mode,
    .pl_vout_flip     = vout_flip,
    .pl_vout_close    = vout_close,
    .mmap             = pl_mmap,
    .munmap           = pl_munmap,
    .gpu_hcnt         = &hSyncCount,
    .gpu_frame_count  = &frame_counter,
    // Disable interlace to prevent 1-pixel vertical bounce on progressive displays
    .gpu_neon.allow_interlace = 0,
};

#include "plugins/dfsound/out.h"

static void (*emu_puts_cb)(const char *) = NULL;
void corelib_set_puts(void (*cb)(const char *)) {
    emu_puts_cb = cb;
}

// Audio driver stub for dfsound
static int apu_init(void) { return 0; }
static void apu_finish(void) {}
static int apu_busy(void) { return 0; }
static void apu_feed(void *data, int bytes) {
    printf("got audio: [%d bytes]\n", bytes);
    int16_t* samples = (int16_t*)data;
    size_t count = bytes / sizeof(int16_t);
    ring_push(&ring_, samples, count);
}

void out_register_libretro(struct out_driver *drv) {
    drv->name = "pico_apu";
    drv->init = apu_init;
    drv->finish = apu_finish;
    drv->busy = apu_busy;
    drv->feed = apu_feed;
}

// Platform/Frontend stubs
void plat_trigger_vibrate(int port, int low, int high) {}
void pl_frame_limit(void) {
    psxRegs.stop++;  // stop execution.
}
void pl_timing_prepare(int is_pal) {}
void pl_gun_byte2(int port, unsigned char byte) {}

const int PLAYER_1 = 0;
EXPOSE
void set_key(size_t key, char val) {
    // analog sticks unsupported, original ps1 didnt have and
    // most games dont need them.
    uint16_t mask = 0;
    switch (key) {
        case BTN_B: mask = 1 << DKEY_CROSS; break;
        case BTN_A: mask = 1 << DKEY_CIRCLE; break;
        case BTN_X: mask = 1 << DKEY_SQUARE; break;
        case BTN_Y: mask = 1 << DKEY_TRIANGLE; break;
        case BTN_Up: mask = 1 << DKEY_UP; break;
        case BTN_Down: mask = 1 << DKEY_DOWN; break;
        case BTN_Left: mask = 1 << DKEY_LEFT; break;
        case BTN_Right: mask = 1 << DKEY_RIGHT; break;
        case BTN_Start: mask = 1 << DKEY_START; break;
        case BTN_Sel: mask = 1 << DKEY_SELECT; break;
        default: return;
    }
    if (val) {
        in_keystate[PLAYER_1] |= mask;
    } else {
        in_keystate[PLAYER_1] &= ~mask;
    }
}

EXPOSE
void init(const uint8_t* data, size_t len) {
    ring_init(&ring_);

    if (psxCpu) {
        puts("Shutting down old core.");
         psxCpu->Notify(R3000ACPU_NOTIFY_BEFORE_SAVE, NULL);
         psxCpu->Shutdown();
    }

    if (Config.Cpu == CPU_INTERPRETER) {
        puts("using interpreter");
        psxCpu = &psxInt;
    } else {
        puts("using recompiler");
        psxCpu = &psxRec;
    }

    // RA: SysReset() called when rebootemu flag is set.
    psxCpu->Init();
    psxCpu->Notify(R3000ACPU_NOTIFY_AFTER_LOAD, NULL);
    // TODO: re-examine extensive config from libretro
    // Config.bios set to path to bios file.
    psxCpu->ApplyConfig();
    emu_core_preinit();
    emu_core_init();

    // Set CD image BEFORE LoadPlugins (needed by cdra_open in OpenPlugins)
    puts("Loading cd");
    set_cd_image("demo.chd");

    if (LoadPlugins() == -1) {
        puts("Failed to load plugins.");
        return;
    }
    if (OpenPlugins() == -1) {
        puts("Failed to open plugins.");
        return;
    }

    if (CheckCdrom() == -1) {
        puts("CheckCdrom failed.");
        return;
    }

    plugin_call_rearmed_cbs();
    SysReset();

    if (LoadCdrom() == -1) {
        puts("LoadCdrom failed.");
        return;
    }
    puts("Loaded cd.");

    // TODO: design mapping to cdrom

    // examine for audio: retro_set_audio_buff_status_cb();
    // video: retro_get_system_av_info
}

EXPOSE
const uint8_t *framebuffer() {
     return (uint8_t*)fbuffer_;
}

EXPOSE
void frame() {
   psxRegs.stop = 0;
   psxCpu->Execute(&psxRegs);
}

EXPOSE
 void dump_state(const char* filename) {
    REQUIRE_CORE();
    int fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY , 0700);
    if (fd == -1) {
        perror("failed to open:");
        return;
    }
    printf("saving to %s\n", filename);
    save(fd);
 }

EXPOSE
void load_state(const char* filename) {
    REQUIRE_CORE();
    int fd = open(filename,  O_RDONLY , 0700);
    if (fd == -1) {
        perror("Failed to open: ");
        return;
    }
    load(fd);
}

// Interface used by app. App closes fd.
EXPOSE
void save(int fd) {
    REQUIRE_CORE();
    int size = save_str(NULL, 0);
    uint8_t *buffer = (uint8_t*)malloc(size);
    save_str(buffer, size);
    const uint8_t* wr = buffer;
    while(size > 0) {
        ssize_t count = write(fd, wr, size);
        if (write < 0) {
            perror("write failed: ");
            exit(1);
            return;
        }
        size -= count;
        wr += count;
    }
    free(buffer);
}

EXPOSE
void load(int fd) {
    REQUIRE_CORE();
    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos < 0) {
        perror("lseek failed: ");
        exit(1);
        return;
    }
    lseek(fd, 0, SEEK_SET);
    uint8_t *buffer = malloc(pos);
    size_t count = pos;
    uint8_t *rd = buffer;
    while (count > 0) {
        ssize_t c = read(fd, rd, count);
        if (c < 0) {
            perror("Read failed: ");
            exit(1);
            return;
        }
        rd += c;
        count -= c;
    }
    load_str(pos, buffer);
    free(buffer);
}

// libpcsx uses bundle of fn ptrs to manage files.
// we pass a ptr to a filebuffer to manage size & prevent oob.
struct FileBuffer {
    uint8_t* buffer;
    size_t len;
};

// Returns bytes saved, and writes to dest. 
// Dest may be null to calculate size only. returns < 0 on error.
EXPOSE 
int save_str(uint8_t* dest, int capacity) {
    return SaveState((char*)&dest);
}
// Loads len bytes from src
EXPOSE 
void load_str(int len, const uint8_t* src) {
    LoadState((const char*)&src);
}

// APU
EXPOSE
long apu_sample_variable(int16_t *output, int32_t frames) {
    size_t received = ring_pull(&ring_, output, frames);
    if (received < frames) {
        printf("underrun, filling %d - %ld frames\n", frames, received);
        int16_t last = received > 0 ? output[received-1] : 0;
        for (int i = received; i < frames; i++) {
            output[i] = last;
        }
    }
    return received;
}
