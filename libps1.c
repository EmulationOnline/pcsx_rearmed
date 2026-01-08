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

EXPOSE
int width() {
    return vout_w;
}
EXPOSE
int height() {
    return vout_h;
}

static char emu_tmpfile[128] = "";
EXPOSE
void set_cachedir(const char* path) {
    if (strlen(path) < sizeof(emu_tmpfile)) {
        snprintf(emu_tmpfile, sizeof(emu_tmpfile), "%s/a.chd", path);
        puts("set emu_tmpfile:");
        puts(emu_tmpfile);
    } else {
        puts("unable to set emu_tmpfile");
        strcpy(emu_tmpfile, "/dev/null");
    }
}

static void vout_flip(const void *vram, int vram_offset, int bgr24,
                      int x, int y, int w, int h, int dims_changed) {
    (void)dims_changed;

    if (vram == NULL)
        return;
    // printf("offset=%d bgr=%d\n", vram_offset, bgr24);

    // vram_offset = 0;  // prevent y bounce from interlace.
    const uint8_t *src = (const uint8_t *)vram;

    // Clamp to our buffer size
    int copy_w = (w < VIDEO_WIDTH) ? w : VIDEO_WIDTH;
    int copy_h = (h < VIDEO_HEIGHT) ? h : VIDEO_HEIGHT;
    // printf("copying (%d , %d)\n", copy_w, copy_h);

    if (bgr24) {
        // bgr24 used typically for FMVs
        // 24-bit BGR888: 3 bytes per pixel
        // Convert row by row since source stride may differ
        // void bgr888_to_xrgb8888(void *dst, const void *src, int bytes);
        int src_stride = w * 3 * 2;  // stridex2 to compensate for interleaved src video.
        for (int row = 0; row < copy_h; row++) {
            bgr888_to_xrgb8888(
                fbuffer_ + row * VIDEO_WIDTH,
                src + vram_offset,
                copy_w * 3
            );
            vram_offset = (vram_offset + 2048) & 0xFFFFF;
        }
    } else {
        // 16-bit BGR555: 2 bytes per pixel
        for (int row = 0; row < copy_h; row++) {
            bgr555_to_xrgb8888(
                fbuffer_ + row * VIDEO_WIDTH,
                src + vram_offset,
                copy_w * 2
            );
            vram_offset = (vram_offset + 2048) & 0xFFFFF;
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

#define puts(arg) emu_puts_cb(arg)
static void (*emu_puts_cb)(const char *) = NULL;
void corelib_set_puts(void (*cb)(const char *)) {
    emu_puts_cb = cb;
}

int framerate() {
    return psxGetFps();
}

// Audio driver stub for dfsound
static int apu_init(void) { return 0; }
static void apu_finish(void) {}
static int apu_busy(void) { return 0; }
static void apu_feed(void *data, int bytes) {
    // printf("got audio: [%d bytes]\n", bytes);
    int16_t* samples = (int16_t*)data;
    size_t count = bytes / sizeof(int16_t) / 2;
    for (int i = 0; i < count; i++) {
        // samples[i] = samples[i*2];
        samples[i] = (samples[i*2] + samples[i*2+1]) / 2;
    }
    size_t written = ring_push(&ring_, samples, count);
    if (written < count) {
        printf("wrote only %d / %d samples to ring\n", written, count);
    }
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

        case BTN_L: mask = 1 << DKEY_L1; break;
        case BTN_L2: mask = 1 << DKEY_L2; break;
        case BTN_R: mask = 1 << DKEY_R1; break;
        case BTN_R2: mask = 1 << DKEY_R2; break;
        default: return;
    }
    if (val) {
        in_keystate[PLAYER_1] |= mask;
    } else {
        in_keystate[PLAYER_1] &= ~mask;
    }
}

const char* mkfile(const uint8_t* data, size_t bytes) {
    const char* path = emu_tmpfile;
    if (strlen(path) == 0) {
        set_cachedir(".");
    }
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0700);
    if (fd < 0) {
        puts("open tmp failed.");
        return path;
    }
    while (bytes > 0) {
        int written = write(fd, data, bytes);
        if (written <= 0) {
            break;
        }
        bytes -= written;
        data += written;
    }
    close(fd);
    return path;
}

extern char Mcd1Data[MCD_SIZE];
extern char Mcd2Data[MCD_SIZE];
extern char McdDisable[2];
void init_memcard(char* memcard) {
    unsigned off = 0;
    unsigned i;

    memset(memcard, 0, MCD_SIZE);

    memcard[off++] = 'M';
    memcard[off++] = 'C';
    off += 0x7d;
    memcard[off++] = 0x0e;

    for (i = 0; i < 15; i++) {
        memcard[off++] = 0xa0;
        off += 0x07;
        memcard[off++] = 0xff;
        memcard[off++] = 0xff;
        off += 0x75;
        memcard[off++] = 0xa0;
    }

    for (i = 0; i < 20; i++) {
        memcard[off++] = 0xff;
        memcard[off++] = 0xff;
        memcard[off++] = 0xff;
        memcard[off++] = 0xff;
        off += 0x04;
        memcard[off++] = 0xff;
        memcard[off++] = 0xff;
        off += 0x76;
    }
}
void init_memcards() {
    snprintf(Config.Mcd1, sizeof(Config.Mcd1), "none");
    snprintf(Config.Mcd2, sizeof(Config.Mcd2), "none");
    init_memcard(Mcd1Data);
    init_memcard(Mcd2Data);
    McdDisable[0] = 0;
    McdDisable[1] = 0;
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
    const char* tmpfile = mkfile(data, len);
    puts("Generated tmpfile.");
    puts(tmpfile);
    // set_cd_image("demo.chd");
    set_cd_image(tmpfile);

    init_memcards();
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

}

// Bios may be unloaded via copy_bios(null, 0);
// Takes effect at next system restart.
EXPOSE
void copy_bios(const uint8_t* buffer, size_t len) {
    psxMemBiosCopy(buffer, len);
}

EXPOSE
const uint8_t *framebuffer() {
     return (uint8_t*)fbuffer_;
}

EXPOSE
void frame() {
    // puts("frame");
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
    printf("Calculated save size: %d\n", size);
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
    // if buffer is nullptr, reads and writes will report
    // success but no-op. This is used by write code to calculate
    // required size.
    uint8_t* buffer;
    size_t len;
    size_t pos;
    char write;
    char closed;
};

void *str_open(const char *name, const char *mode) {
    struct FileBuffer* buf = (struct FileBuffer*)name;
    if (strcmp(mode, "wb") == 0 && buf->write) {
        if (buf->buffer == 0) {
            puts("setting null buffer.len to MAX_SIZE for estimation.");
            buf->len = SIZE_MAX;
        }
    } else if (strcmp(mode, "rb") == 0 && !buf->write) {
    } else {
        printf("unsupported mode for str_open: %s, buf.write=%d\n", mode,
                buf->write);
        return NULL;
    }
    buf->pos = 0;
    buf->closed = 0;
    return (void*)buf;
}

// returns bytes read or <= 0 on error
int str_read(void *file, void *dst, u32 len) {
    struct FileBuffer* buf = (struct FileBuffer*)file;
    if (buf->closed) {
        puts("Cannot write, str_buf already closed.");
        return -1;
    }
    if (buf->pos < buf->len) {
        size_t rem = buf->len - buf->pos;
        len = len < rem ? len : rem;
        if (buf->buffer) {
            memcpy(dst, buf->buffer + buf->pos, len);
        }
        buf->pos += len;
        // printf("read %d bytes\n", len);
    } else {
        puts("str_read already at EOF.");
        return 0;
    }
}

// returns bytes written or <= 0 on error
int str_write(void *file, const void *src, u32 len) {
    struct FileBuffer* buf = (struct FileBuffer*)file;
    if (buf->closed) {
        puts("cannot write, str_buf already closed.");
        return 0;
    }
    if (buf->pos < buf->len) {
        size_t rem = buf->len - buf->pos;
        len = len < rem ? len : rem;
        if (buf->buffer) {
            memcpy(buf->buffer + buf->pos, src, len);
        }
        buf->pos += len;
        // printf("wrote %d bytes\n", len);
    } else {
        puts("str_write already at EOF.");
        return 0;
    }
}
long  str_seek(void *file, long offs, int whence) {
    struct FileBuffer* buf = (struct FileBuffer*)file;
    int newpos = 0;
    switch (whence) {
        case SEEK_SET:
            puts("str SEEK_SET");
            // The file offset is set to offset bytes.
            // NOTE: unix lseek allows increasing size of file / writing
            // beyond by tracking the 'hole size'. We dont support that
            // on strings, so will fail if seeked OOB.
            newpos = offs;
            break;
        case SEEK_CUR:
            puts("str SEEK_CUR");
            // The file offset is set to its current location plus offset bytes.
            newpos = offs + buf->pos;
            break;
        case SEEK_END:
            puts("str SEEK_END");
            // The file offset is set to the size of the file plus offset bytes.
            newpos = offs + buf->len;
            break;
        default:
            printf("unknown WHENCE(%d) for str_seek, skipping\n", whence);
            return buf->pos;
    }
    if (newpos >= 0 && newpos < buf->len) {
        buf->pos = newpos;
    } else {
        puts("Cannot str_seek OOB, ignoring.");
    }
    return buf->pos;
}
void str_close(void *file) {
    struct FileBuffer* buf = (struct FileBuffer*)file;
    buf->closed = 1;
}

void strSaveFuncs(uint8_t* buffer, size_t len) {
    // len represents buffer capacity for saving, or
    // size of blob for loading.
    struct PcsxSaveFuncs funcs = {
        .open = str_open,
        .read = str_read,
        .write = str_write,
        .seek = str_seek,
        .close = str_close,
    };
    SaveFuncs = funcs;
}

// Returns bytes saved, and writes to dest. 
// Dest may be null to calculate size only. returns < 0 on error.
EXPOSE 
int save_str(uint8_t* dest, int capacity) {
    struct PcsxSaveFuncs oldSaveFuncs = SaveFuncs;
    if (dest) {
        memcpy(dest, Mcd1Data, MCD_SIZE); dest += MCD_SIZE; capacity -= MCD_SIZE;
        memcpy(dest, Mcd2Data, MCD_SIZE); dest += MCD_SIZE; capacity -= MCD_SIZE;
    }
    strSaveFuncs(dest, capacity);
    struct FileBuffer buf = {
        .buffer = dest,
        .len = capacity,
        .pos = 0,
        .write = 1,
        .closed = 0,
    };
    int ret = SaveState((char*)&buf);
    SaveFuncs = oldSaveFuncs;
    return buf.pos + 2*MCD_SIZE;
}
// Loads len bytes from src
EXPOSE 
void load_str(int len, const uint8_t* src) {
    // mem 1, mem2, then state snapshot
    if (len < 2*MCD_SIZE) {
        puts("save is too small to load memcard 1+2");
        return;
    }
    memcpy(Mcd1Data, src, MCD_SIZE); src += MCD_SIZE; len -= MCD_SIZE;
    memcpy(Mcd2Data, src, MCD_SIZE); src += MCD_SIZE; len -= MCD_SIZE;
    struct PcsxSaveFuncs oldSaveFuncs = SaveFuncs;
    strSaveFuncs(src, len);
    struct FileBuffer buf = {
        .buffer = src,
        .len = len,
        .pos = 0,
        .write = 0,
        .closed = 0,
    };
    LoadState((const char*)&buf);
    SaveFuncs = oldSaveFuncs;
}

// APU
EXPOSE
long apu_sample_variable(int16_t *output, int32_t frames) {
    size_t received = ring_pull(&ring_, output, frames);
    if (received < frames) {
        printf("underrun, filling %d - %ld frames\n", frames, received);
        // int16_t last = received > 0 ? output[received-1] : 0;
        for (int i = received; i < frames; i++) {
            output[i] = 0;
        }
    }
    return received;
}
