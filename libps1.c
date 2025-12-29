#include "corelib.h"
#include "../libpcsxcore/misc.h"
#include "../libpcsxcore/psxcounters.h"
#include "../libpcsxcore/psxmem_map.h"
#include "../libpcsxcore/new_dynarec/new_dynarec.h"
#include "../libpcsxcore/cdrom.h"
#include "../libpcsxcore/cdrom-async.h"
#include "../libpcsxcore/cdriso.h"
#include "../libpcsxcore/cheat.h"
#include "../libpcsxcore/r3000a.h"
#include "../libpcsxcore/gpu.h"
#include "../libpcsxcore/database.h"
#include "../plugins/dfsound/out.h"
#include "../plugins/dfsound/spu_config.h"
// #include "cspace.h"
#include "frontend/main.h"
// #include "menu.h"
// #include "plugin.h"
// #include "plugin_lib.h"
// #include "arm_features.h"
// #include "revision.h"


R3000Acpu *psxCore = NULL;
uint32_t fbuffer_[VIDEO_WIDTH * VIDEO_HEIGHT];

EXPOSE
void set_key(size_t key, char val) {
     // examine in_keystate or input_state_cb
}

EXPOSE
void init(const uint8_t* data, size_t len) {
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
