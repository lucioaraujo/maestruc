// Maestruc — carregamento da API do JACK.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "Jack.h"

#include <mutex>

#if !defined(__linux__)

// JACK só no Linux (PipeWire). No Windows e no macOS o Maestruc usa o modo
// portátil: a placa pelo JUCE e o som dos apps pela ponte (18/11/2026).
namespace maestruc::jack {
const Api* load(std::string& error) {
    error = "JACK só no Linux; modo portátil";
    return nullptr;
}
} // namespace maestruc::jack

#else

#include <dlfcn.h>

namespace maestruc::jack {

namespace {

template <typename F>
bool sym(void* lib, const char* name, F& out, std::string& missing) {
    out = reinterpret_cast<F>(dlsym(lib, name));
    if (!out) missing += std::string(missing.empty() ? "" : ", ") + name;
    return out != nullptr;
}

} // namespace

const Api* load(std::string& error) {
    static Api api;
    static bool ok = false;
    static std::string loadError;
    static std::once_flag once;
    std::call_once(once, [] {
        const char* candidates[] = {
            "/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/aarch64-linux-gnu/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib64/pipewire-0.3/jack/libjack.so.0",
            "/usr/lib/pipewire-0.3/jack/libjack.so.0",
            "libjack.so.0",
        };
        void* lib = nullptr;
        for (const char* c : candidates)
            if ((lib = dlopen(c, RTLD_NOW | RTLD_LOCAL)) != nullptr) break;
        if (!lib) { loadError = "biblioteca do JACK não encontrada (instale pipewire-jack)"; return; }
        std::string missing;
        bool all = true;
        all &= sym(lib, "jack_client_open", api.client_open, missing);
        all &= sym(lib, "jack_client_close", api.client_close, missing);
        all &= sym(lib, "jack_activate", api.activate, missing);
        all &= sym(lib, "jack_deactivate", api.deactivate, missing);
        all &= sym(lib, "jack_get_sample_rate", api.get_sample_rate, missing);
        all &= sym(lib, "jack_get_buffer_size", api.get_buffer_size, missing);
        all &= sym(lib, "jack_set_process_callback", api.set_process_callback, missing);
        all &= sym(lib, "jack_on_shutdown", api.on_shutdown, missing);
        all &= sym(lib, "jack_port_register", api.port_register, missing);
        all &= sym(lib, "jack_port_get_buffer", api.port_get_buffer, missing);
        all &= sym(lib, "jack_port_name", api.port_name, missing);
        all &= sym(lib, "jack_get_ports", api.get_ports, missing);
        all &= sym(lib, "jack_port_by_name", api.port_by_name, missing);
        all &= sym(lib, "jack_port_get_all_connections", api.port_get_all_connections, missing);
        all &= sym(lib, "jack_connect", api.connect, missing);
        all &= sym(lib, "jack_disconnect", api.disconnect, missing);
        all &= sym(lib, "jack_free", api.free, missing);
        if (!all) { loadError = "JACK incompleto, faltam: " + missing; return; }
        ok = true;
    });
    if (!ok) { error = loadError; return nullptr; }
    return &api;
}

} // namespace maestruc::jack

#endif
