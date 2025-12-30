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
// #include "cspace.h"
#include "frontend/main.h"
// #include "menu.h"
// #include "plugin.h"
// #include "plugin_lib.h"
// #include "arm_features.h"
// #include "revision.h"


#include <stdarg.h>
#include <stdio.h>

#include "frontend/plugin_lib.h"

R3000Acpu *psxCore = NULL;
uint32_t fbuffer_[VIDEO_WIDTH * VIDEO_HEIGHT];

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
struct rearmed_cbs pl_rearmed_cbs;

#include "plugins/dfsound/out.h"

static void (*emu_puts_cb)(const char *) = NULL;
void corelib_set_puts(void (*cb)(const char *)) {
    emu_puts_cb = cb;
}

// Audio driver stub for dfsound
static int dummy_init(void) { return 0; }
static void dummy_finish(void) {}
static int dummy_busy(void) { return 0; }
static void dummy_feed(void *data, int bytes) {}

void out_register_libretro(struct out_driver *drv) {
    drv->name = "libretro_dummy";
    drv->init = dummy_init;
    drv->finish = dummy_finish;
    drv->busy = dummy_busy;
    drv->feed = dummy_feed;
}

// Platform/Frontend stubs
void plat_trigger_vibrate(int port, int low, int high) {}
void pl_frame_limit(void) {}
void pl_timing_prepare(int is_pal) {}
void pl_gun_byte2(int port, unsigned char byte) {}

EXPOSE
void set_key(size_t key, char val) {
     // examine in_keystate or input_state_cb
}

EXPOSE
void init(const uint8_t* data, size_t len) {
    if (psxCore) {
        puts("Shutting down old core.");
         psxCore->Notify(R3000ACPU_NOTIFY_BEFORE_SAVE, NULL);
         psxCore->Shutdown();
    }

    if (Config.Cpu == CPU_INTERPRETER) {
        puts("using interpreter");
        psxCore = &psxInt;
    } else {
        puts("using recompiler");
        psxCore = &psxRec;
    }

    // RA: SysReset() called when rebootemu flag is set.
    psxCore->Init();
    psxCore->Notify(R3000ACPU_NOTIFY_AFTER_LOAD, NULL);
    // TODO: re-examine extensive config from libretro
    // Config.bios set to path to bios file.
    psxCore->ApplyConfig();
    // emu_core_preinit();
    emu_core_init();

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
   // regs.stop = 0
   psxCore->Execute(&psxRegs);
}

EXPOSE
 void dump_state(const char* save_path) {}

EXPOSE
void load_state(const char* save_path) {}

// Interface used by app. App closes fd.
EXPOSE
void save(int fd) {}

EXPOSE
void load(int fd) {}

// APU
EXPOSE
long apu_sample_variable(int16_t *output, int32_t frames) {
    return 0;
}
