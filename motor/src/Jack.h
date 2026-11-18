// Maestruc — acesso mínimo à API C do JACK, carregada em tempo de execução.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Não exige o pacote de desenvolvimento do JACK: as poucas funções usadas são
// declaradas aqui (ABI C estável) e buscadas com dlopen, primeiro na
// biblioteca do PipeWire (pipewire-jack), depois na do sistema. Mesmo método
// do JUCE (juce_JackAudio.cpp), escrito à parte.
#pragma once

#include <cstdint>
#include <string>

namespace maestruc::jack {

using nframes_t = std::uint32_t;
struct client_t;
struct port_t;
using ProcessCallback = int (*)(nframes_t, void*);
using ShutdownCallback = void (*)(void*);

constexpr unsigned long PortIsInput = 0x1;
constexpr unsigned long PortIsOutput = 0x2;
constexpr unsigned long PortIsPhysical = 0x4;
constexpr int NoStartServer = 0x01;
constexpr const char* DefaultAudioType = "32 bit float mono audio";

struct Api {
    client_t* (*client_open)(const char*, int, int*, ...) = nullptr;
    int (*client_close)(client_t*) = nullptr;
    int (*activate)(client_t*) = nullptr;
    int (*deactivate)(client_t*) = nullptr;
    nframes_t (*get_sample_rate)(client_t*) = nullptr;
    nframes_t (*get_buffer_size)(client_t*) = nullptr;
    int (*set_process_callback)(client_t*, ProcessCallback, void*) = nullptr;
    void (*on_shutdown)(client_t*, ShutdownCallback, void*) = nullptr;
    port_t* (*port_register)(client_t*, const char*, const char*, unsigned long, unsigned long) = nullptr;
    void* (*port_get_buffer)(port_t*, nframes_t) = nullptr;
    const char* (*port_name)(const port_t*) = nullptr;
    const char** (*get_ports)(client_t*, const char*, const char*, unsigned long) = nullptr;
    port_t* (*port_by_name)(client_t*, const char*) = nullptr;
    const char** (*port_get_all_connections)(const client_t*, const port_t*) = nullptr;
    int (*connect)(client_t*, const char*, const char*) = nullptr;
    int (*disconnect)(client_t*, const char*, const char*) = nullptr;
    void (*free)(void*) = nullptr;
};

// Carrega uma vez; devolve nullptr (e explica em `error`) se não houver JACK.
const Api* load(std::string& error);

} // namespace maestruc::jack
