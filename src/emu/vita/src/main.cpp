/*
 * Copyright (c) 2026 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 */

#include <vita/state.h>
#include <vita/thread.h>

#include <SDL.h>

#if defined(__vita__) || defined(__PSP2__)
#include <psp2/kernel/processmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#endif

#if defined(__vita__) || defined(__PSP2__)
namespace {
    class vita_network_runtime {
    public:
        vita_network_runtime() {
            module_loaded_ = sceSysmoduleLoadModule(SCE_SYSMODULE_NET) >= 0;
            if (!module_loaded_) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load Vita network module");
                return;
            }

            SceNetInitParam init_param{};
            init_param.memory = net_memory_;
            init_param.size = sizeof(net_memory_);
            init_param.flags = 0;

            net_initialized_ = sceNetInit(&init_param) >= 0;
            if (!net_initialized_) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize Vita network stack");
                return;
            }

            ctl_initialized_ = sceNetCtlInit() >= 0;
            if (!ctl_initialized_) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize Vita network control");
            }
        }

        ~vita_network_runtime() {
            if (ctl_initialized_) {
                sceNetCtlTerm();
            }

            if (net_initialized_) {
                sceNetTerm();
            }

            if (module_loaded_) {
                sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
            }
        }

    private:
        alignas(64) static unsigned char net_memory_[1024 * 1024];
        bool module_loaded_ = false;
        bool net_initialized_ = false;
        bool ctl_initialized_ = false;
    };

    alignas(64) unsigned char vita_network_runtime::net_memory_[1024 * 1024];
}
#endif

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SDL_SetMainReady();

#if defined(__vita__) || defined(__PSP2__)
    vita_network_runtime network_runtime;
#endif

    eka2l1::vita::emulator state;
    const bool started = eka2l1::vita::emulator_entry(state);
    if (started) {
        eka2l1::vita::start_threads(state);
    }

    while (started && !state.should_emu_quit) {
        SDL_Delay(16);
    }

    eka2l1::vita::stop_threads(state);
    SDL_Quit();

#if defined(__vita__) || defined(__PSP2__)
    sceKernelExitProcess(0);
#endif

    return started ? 0 : 1;
}
