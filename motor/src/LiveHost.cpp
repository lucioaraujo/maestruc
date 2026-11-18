// Maestruc — host ao vivo.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "LiveHost.h"
#include "VersaoEntrega.h"

#include <filesystem>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace maestruc {

namespace {
constexpr int kMaxLiveBlock = 8192;
}

bool LiveHost::open(const std::string& clientName, const std::vector<std::string>& stems, std::string& error,
                    int capacity) {
    close();
    capacity_ = std::max(static_cast<int>(stems.size()), capacity);
    if (capacity_ < 1 || capacity_ > kMaxStrips) {
        error = "entre 1 e " + std::to_string(kMaxStrips) + " stems";
        return false;
    }
    api_ = jack::load(error);
    if (!api_) return false;
    int status = 0;
    client_ = api_->client_open(clientName.c_str(), jack::NoStartServer, &status);
    if (!client_) { error = "não abriu o cliente JACK (PipeWire rodando?) status " + std::to_string(status); return false; }
    sampleRate_ = static_cast<int>(api_->get_sample_rate(client_));
    maxBlock_ = std::max(kMaxLiveBlock, static_cast<int>(api_->get_buffer_size(client_)));
    stems_.clear();
    inPorts_.assign(static_cast<std::size_t>(capacity_) * 2, nullptr);
    activeStems_ = 0;
    const char* outs[] = { "master_L", "master_R", "fones_L", "fones_R" };
    for (int i = 0; i < 4; ++i) {
        outPorts_[i] = api_->port_register(client_, outs[i], jack::DefaultAudioType, jack::PortIsOutput, 0);
        if (!outPorts_[i]) { error = std::string("não registrou a porta ") + outs[i]; close(); return false; }
    }
    mixer_.prepare(sampleRate_, maxBlock_, capacity_, 0);
    inPtr_.assign(static_cast<std::size_t>(capacity_) * 2, nullptr);
    post_.assign(static_cast<std::size_t>(capacity_) * 2, std::vector<float>(static_cast<std::size_t>(maxBlock_)));
    postPtr_.clear();
    for (auto& b : post_) postPtr_.push_back(b.data());
    for (const auto& s : stems)
        if (addStem(s, error) < 0) { close(); return false; }
    shutdown_ = false;
    api_->set_process_callback(client_, &LiveHost::processThunk, this);
    api_->on_shutdown(client_, &LiveHost::shutdownThunk, this);
    if (api_->activate(client_) != 0) { error = "não ativou o cliente JACK"; close(); return false; }
    return true;
}

bool LiveHost::openPortable(int sampleRate, int maxBlock, int capacity, std::string& error) {
    close();
    capacity_ = capacity;
    if (capacity_ < 1 || capacity_ > kMaxStrips) {
        error = "entre 1 e " + std::to_string(kMaxStrips) + " stems";
        return false;
    }
    if (sampleRate < 8000 || maxBlock < 16 || maxBlock > 65536) { error = "taxa ou bloco inválidos"; return false; }
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    stems_.clear();
    inPorts_.assign(static_cast<std::size_t>(capacity_) * 2, nullptr);
    activeStems_ = 0;
    mixer_.prepare(sampleRate_, maxBlock_, capacity_, 0);
    inPtr_.assign(static_cast<std::size_t>(capacity_) * 2, nullptr);
    post_.assign(static_cast<std::size_t>(capacity_) * 2, std::vector<float>(static_cast<std::size_t>(maxBlock_)));
    postPtr_.clear();
    for (auto& b : post_) postPtr_.push_back(b.data());
    silence_.assign(2, std::vector<float>(static_cast<std::size_t>(maxBlock_), 0.0f));
    shutdown_ = false;
    portable_ = true;
    return true;
}

int LiveHost::addStem(const std::string& name, std::string& error) {
    if (!client_ && !portable_) { error = "host fechado"; return -1; }
    if (const int i = stemIndex(name); i >= 0) return i;
    if (name.empty() || name == "master" || name == "fones" || name.find_first_of(":") != std::string::npos) {
        error = "nome de stem inválido: " + name;
        return -1;
    }
    const int idx = static_cast<int>(stems_.size());
    if (idx >= capacity_) { error = "sem lugar para mais stems (" + std::to_string(capacity_) + ")"; return -1; }
    jack::port_t* ports[2] {};
    const char* sides[] = { "_L", "_R" };
    for (int c = 0; c < 2 && client_; ++c) {
        // Nomes de porta JACK não aceitam "/": o stem de um canal interno
        // ("Batruc/bumbo") vira "Batruc-bumbo" na porta.
        std::string porta = name;
        std::replace(porta.begin(), porta.end(), '/', '-');
        ports[c] = api_->port_register(client_, (porta + sides[c]).c_str(), jack::DefaultAudioType, jack::PortIsInput, 0);
        if (!ports[c]) { error = "não registrou a porta " + porta + sides[c]; return -1; }
    }
    inPorts_[static_cast<std::size_t>(idx) * 2] = ports[0];
    inPorts_[static_cast<std::size_t>(idx) * 2 + 1] = ports[1];
    stems_.push_back(name);
    mixer_.setActiveStrips(idx + 1);
    activeStems_.store(idx + 1, std::memory_order_release);
    return idx;
}

void LiveHost::close() {
    if (!client_ && !portable_) return;
    stopRecording();
    // Solta as pontes antes de parar o áudio: os apps voltam a tocar sozinhos.
    for (auto& b : bridges_) b.store(nullptr, std::memory_order_release);
    if (client_) {
        restoreTaken();
        api_->deactivate(client_);
        api_->client_close(client_);
        client_ = nullptr;
    }
    for (auto& b : bridgeOwners_) b->reader.fechar();
    bridgeOwners_.clear();
    portable_ = false;
}

std::vector<int> LiveHost::attachBridge(const std::string& stem, const std::string& path, std::string& error,
                                       int maxPairs) {
    std::vector<int> made;
    if (!running()) { error = "host fechado"; return made; }
    if (hasBridge(stem)) { error = stem + " já tem ponte"; return made; }
    auto b = std::make_unique<Bridge>();
    b->stem = stem;
    if (!b->reader.abrir(path, sampleRate_)) { error = "ponte de som recusada ou ilegível: " + path; return made; }
    const int pairs = b->reader.pares();
    const int used = maxPairs > 0 ? std::min(maxPairs, pairs) : pairs;
    for (int p = 0; p < used; ++p) {
        const std::string nome = p == 0 ? stem : stem + "/" + b->reader.nomeDoPar(p);
        const int s = addStem(nome, error);
        if (s < 0) { b->reader.fechar(); return {}; }
        b->stems.push_back(s);
        made.push_back(s);
    }
    b->buf.assign(static_cast<std::size_t>(pairs) * 2, std::vector<float>(static_cast<std::size_t>(maxBlock_), 0.0f));
    for (auto& v : b->buf) b->ptr.push_back(v.data());
    // um lugar livre na tabela lida pelo áudio
    for (auto& slot : bridges_)
        if (slot.load(std::memory_order_acquire) == nullptr) {
            b->reader.pedirCalar(true);
            slot.store(b.get(), std::memory_order_release);
            bridgeOwners_.push_back(std::move(b));
            return made;
        }
    error = "sem lugar para mais pontes";
    b->reader.fechar();
    return {};
}

void LiveHost::detachBridge(const std::string& stem) {
    for (std::size_t i = 0; i < bridgeOwners_.size(); ++i) {
        auto* b = bridgeOwners_[i].get();
        if (b->stem != stem) continue;
        for (auto& slot : bridges_)
            if (slot.load(std::memory_order_acquire) == b) slot.store(nullptr, std::memory_order_release);
        waitCycles(2);   // a thread de áudio não usa mais a ponte
        b->reader.fechar();   // tira o pedido de calar: o app volta a tocar sozinho
        bridgeOwners_.erase(bridgeOwners_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

bool LiveHost::hasBridge(const std::string& stem) const {
    for (const auto& b : bridgeOwners_) if (b->stem == stem) return true;
    return false;
}

LiveHost::BridgeStatus LiveHost::bridgeStatus(const std::string& stem) const {
    BridgeStatus st;
    for (const auto& b : bridgeOwners_)
        if (b->stem == stem) {
            st.open = b->reader.aberto();
            st.waiting = b->reader.esperando();
            st.gaps = b->reader.faltas();
            st.jumps = b->reader.saltos();
            st.lost = b->reader.perdidosNoInstrumento();
            st.ratio = b->reader.razaoAtual();
        }
    return st;
}

int LiveHost::stemIndex(const std::string& name) const {
    auto it = std::find(stems_.begin(), stems_.end(), name);
    return it == stems_.end() ? -1 : static_cast<int>(it - stems_.begin());
}

std::string LiveHost::fullPortName(const std::string& shortName) const {
    if (!client_) return {};
    for (auto* p : inPorts_) {
        if (!p) continue;
        std::string full = api_->port_name(p);
        if (full.size() > shortName.size() && full.compare(full.size() - shortName.size(), shortName.size(), shortName) == 0
            && full[full.size() - shortName.size() - 1] == ':')
            return full;
    }
    for (auto* p : outPorts_) {
        std::string full = api_->port_name(p);
        if (full.size() > shortName.size() && full.compare(full.size() - shortName.size(), shortName.size(), shortName) == 0
            && full[full.size() - shortName.size() - 1] == ':')
            return full;
    }
    return {};
}

std::vector<std::string> LiveHost::outputPorts(const std::string& pattern) const {
    std::vector<std::string> out;
    if (!client_) return out;
    const char** ports = api_->get_ports(client_, pattern.empty() ? nullptr : pattern.c_str(),
                                         jack::DefaultAudioType, jack::PortIsOutput);
    if (!ports) return out;
    const std::string self = api_->port_name(outPorts_[0]);
    const std::string selfPrefix = self.substr(0, self.find(':') + 1);
    for (int i = 0; ports[i]; ++i)
        if (std::strncmp(ports[i], selfPrefix.c_str(), selfPrefix.size()) != 0) out.emplace_back(ports[i]);
    api_->free(ports);
    return out;
}

std::vector<std::string> LiveHost::connectSource(const std::string& stem, const std::string& pattern, std::string& error,
                                                bool takeFromPlayback) {
    std::vector<std::string> done;
    if (!client_) { error = "sem JACK (modo portátil): o som só chega pela ponte"; return done; }
    const int s = stemIndex(stem);
    if (s < 0) { error = "stem desconhecido: " + stem; return done; }
    // Portas recém-criadas por outro cliente demoram alguns ms a aparecer.
    auto src = outputPorts(pattern);
    for (int i = 0; src.empty() && i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        src = outputPorts(pattern);
    }
    if (src.empty()) { error = "nenhuma saída de áudio corresponde a \"" + pattern + "\""; return done; }
    const std::string l = api_->port_name(inPorts_[static_cast<std::size_t>(s) * 2]);
    const std::string r = api_->port_name(inPorts_[static_cast<std::size_t>(s) * 2 + 1]);
    auto link = [&](const std::string& a, const std::string& b) {
        const int rc = api_->connect(client_, a.c_str(), b.c_str());
        if (rc == 0 || rc == 17 /* EEXIST: já ligado */) done.push_back(a + " → " + b);
        else error = "não ligou " + a + " → " + b;
    };
    if (src.size() == 1) { link(src[0], l); link(src[0], r); }
    else { link(src[0], l); link(src[1], r); }
    if (src.size() > 2) error = std::to_string(src.size()) + " portas correspondem; liguei as 2 primeiras (refine o padrão)";
    if (takeFromPlayback && !done.empty()) {
        for (std::size_t i = 0; i < std::min<std::size_t>(2, src.size()); ++i)
            if (std::find(takenSources_.begin(), takenSources_.end(), src[i]) == takenSources_.end())
                takenSources_.push_back(src[i]);
        enforceTaken();
    }
    return done;
}

std::vector<std::string> LiveHost::physicalPlayback() const {
    std::vector<std::string> out;
    const char** ports = api_->get_ports(client_, nullptr, jack::DefaultAudioType, jack::PortIsInput | jack::PortIsPhysical);
    if (!ports) return out;
    for (int i = 0; ports[i]; ++i) out.emplace_back(ports[i]);
    api_->free(ports);
    return out;
}

std::vector<std::string> LiveHost::connectionsOf(const std::string& port) const {
    std::vector<std::string> out;
    jack::port_t* p = api_->port_by_name(client_, port.c_str());
    if (!p) return out;
    const char** c = api_->port_get_all_connections(client_, p);
    if (!c) return out;
    for (int i = 0; c[i]; ++i) out.emplace_back(c[i]);
    api_->free(c);
    return out;
}

int LiveHost::enforceTaken() {
    if (!client_ || takenSources_.empty()) return 0;
    const auto phys = physicalPlayback();
    int cut = 0;
    for (const auto& src : takenSources_)
        for (const auto& dst : connectionsOf(src)) {
            if (std::find(phys.begin(), phys.end(), dst) == phys.end()) continue;
            if (api_->disconnect(client_, src.c_str(), dst.c_str()) == 0) {
                ++cut;
                const std::pair<std::string, std::string> link { src, dst };
                if (std::find(taken_.begin(), taken_.end(), link) == taken_.end()) taken_.push_back(link);
            }
        }
    return cut;
}

void LiveHost::restoreTaken() {
    if (!client_) return;
    for (const auto& [src, dst] : taken_)
        if (api_->port_by_name(client_, src.c_str())) api_->connect(client_, src.c_str(), dst.c_str());
    taken_.clear();
    takenSources_.clear();
}

bool LiveHost::connectMasterToPlayback(std::string& error) {
    if (!client_) { error = "sem JACK (modo portátil)"; return false; }
    const char** ports = api_->get_ports(client_, nullptr, jack::DefaultAudioType, jack::PortIsInput | jack::PortIsPhysical);
    if (!ports || !ports[0]) { if (ports) api_->free(ports); error = "nenhuma saída física"; return false; }
    bool ok = api_->connect(client_, api_->port_name(outPorts_[0]), ports[0]) == 0;
    ok = api_->connect(client_, api_->port_name(outPorts_[1]), ports[1] ? ports[1] : ports[0]) == 0 && ok;
    api_->free(ports);
    if (!ok) error = "não ligou o master às caixas";
    return ok;
}

std::vector<std::string> LiveHost::playbackDevices() const {
    std::vector<std::string> out;
    if (!client_) return out;
    for (const auto& port : physicalPlayback()) {
        const auto dev = port.substr(0, port.rfind(':'));
        if (std::find(out.begin(), out.end(), dev) == out.end()) out.push_back(dev);
    }
    return out;
}

bool LiveHost::routeBus(int bus, const std::string& device, std::string& error) {
    if (!client_ || bus < 0 || bus > 1) { error = "host fechado"; return false; }
    jack::port_t* ports[2] = { outPorts_[bus * 2], outPorts_[bus * 2 + 1] };
    for (auto* p : ports) {
        const std::string name = api_->port_name(p);
        for (const auto& dst : connectionsOf(name)) api_->disconnect(client_, name.c_str(), dst.c_str());
    }
    if (device.empty()) return true;
    std::vector<std::string> dev;
    for (const auto& port : physicalPlayback())
        if (port.compare(0, device.size() + 1, device + ":") == 0) dev.push_back(port);
    if (dev.empty()) { error = "aparelho de saída não encontrado: " + device; return false; }
    bool ok = api_->connect(client_, api_->port_name(ports[0]), dev[0].c_str()) == 0;
    ok = api_->connect(client_, api_->port_name(ports[1]), (dev.size() > 1 ? dev[1] : dev[0]).c_str()) == 0 && ok;
    if (!ok) error = "não ligou a " + device;
    return ok;
}

bool LiveHost::startRecording(const std::string& dir, std::string& error,
                              const std::vector<bool>* stemOn, bool masterOn) {
    if (!running()) { error = "host fechado"; return false; }
    stopRecording();
    std::vector<std::string> names;
    recMap_.fill(-1);
    recStems_ = static_cast<int>(stems_.size());
    for (int s = 0; s < recStems_; ++s)
        if (!stemOn || (static_cast<std::size_t>(s) < stemOn->size() && (*stemOn)[static_cast<std::size_t>(s)])) {
            recMap_[static_cast<std::size_t>(s)] = static_cast<int>(names.size());
            names.push_back(stems_[static_cast<std::size_t>(s)]);
        }
    if (masterOn) {
        recMap_[kMaxStrips] = static_cast<int>(names.size());
        names.push_back("master");
    }
    if (names.empty()) { error = "nenhuma pista escolhida"; return false; }
    auto rec = std::make_unique<Recorder>();
    if (!rec->start(dir, names, sampleRate_, 2.0, error)) return false;
    recDir_ = dir;
    recMaster_ = masterOn;
    recorderOwner_ = std::move(rec);
    recorder_.store(recorderOwner_.get(), std::memory_order_release);
    return true;
}

bool LiveHost::stopRecording() {
    if (!recorderOwner_) return true;
    recorder_.store(nullptr, std::memory_order_release);
    waitCycles(2);   // a thread de áudio não usa mais o gravador
    const bool ok = recorderOwner_->stop();
    lastDropped_ = recorderOwner_->droppedFrames();
    recorderOwner_.reset();
    // Versão de entrega do Master (16/11/2026, saida-ref 1.1): num fio próprio,
    // em blocos; as pistas por stem seguem cruas. Uma gravação nova espera a
    // anterior terminar (join), e o destrutor também.
    if (ok && recMaster_) {
        if (entrega_.joinable()) entrega_.join();
        const std::string master = (std::filesystem::path(recDir_) / "master.wav").string();
        entrega_ = std::thread([master] {
            std::string resumo;
            escreverVersaoDeEntrega(master, resumo);
        });
    }
    recMaster_ = false;
    return ok;
}

void LiveHost::waitCycles(int count) {
    const auto start = cycles_.load();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (cycles_.load() < start + static_cast<std::uint64_t>(count) && running()
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

int LiveHost::processThunk(jack::nframes_t n, void* self) { return static_cast<LiveHost*>(self)->process(n); }
void LiveHost::shutdownThunk(void* self) { static_cast<LiveHost*>(self)->shutdown_ = true; }

int LiveHost::process(jack::nframes_t nframes) {
    float* out[4];
    for (int i = 0; i < 4; ++i) out[i] = static_cast<float*>(api_->port_get_buffer(outPorts_[i], nframes));
    processCore(out, static_cast<int>(nframes), true, nframes);
    return 0;
}

void LiveHost::processPortable(float* const* outIn, int frames) {
    float* out[4] = { outIn[0], outIn[1], outIn[2], outIn[3] };
    // sem fones na placa: o barramento de fones vai para um rascunho mudo
    if (!out[2] || !out[3]) {
        if (frames <= maxBlock_) { out[2] = silence_[0].data(); out[3] = silence_[1].data(); }
    }
    processCore(out, frames, false, 0);
    if (frames <= maxBlock_ && out[2] == silence_[0].data()) {
        std::fill(silence_[0].begin(), silence_[0].begin() + frames, 0.0f);
        std::fill(silence_[1].begin(), silence_[1].begin() + frames, 0.0f);
    }
}

void LiveHost::processCore(float* const* out, int frames, bool fromJack, jack::nframes_t nframes) {
    if (frames > maxBlock_) {
        for (int i = 0; i < 4; ++i) if (out[i]) std::memset(out[i], 0, sizeof(float) * static_cast<std::size_t>(frames));
        oversized_.fetch_add(1, std::memory_order_relaxed);
        cycles_.fetch_add(1, std::memory_order_release);
        return;
    }
    const int n = activeStems_.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < static_cast<std::size_t>(n) * 2; ++i)
        inPtr_[i] = fromJack && inPorts_[i] ? static_cast<const float*>(api_->port_get_buffer(inPorts_[i], nframes)) : nullptr;
    // Pontes de som: cada uma lê todos os pares de uma vez e os entrega aos
    // stems dela (substituem a porta JACK, se houver).
    for (auto& slot : bridges_) {
        Bridge* b = slot.load(std::memory_order_acquire);
        if (!b) continue;
        b->reader.ler(b->ptr.data(), static_cast<int>(b->ptr.size()), frames);
        for (std::size_t p = 0; p < b->stems.size(); ++p) {
            const int s = b->stems[p];
            if (s < 0 || s >= n) continue;
            inPtr_[static_cast<std::size_t>(s) * 2] = b->ptr[p * 2];
            inPtr_[static_cast<std::size_t>(s) * 2 + 1] = b->ptr[p * 2 + 1];
        }
    }
    mixer_.process(inPtr_.data(), out[0], out[1], out[2], out[3], postPtr_.data(), frames);
    if (auto* rec = recorder_.load(std::memory_order_acquire)) {
        const int m = std::min(n, recStems_);
        for (int s = 0; s < m; ++s)
            if (const int t = recMap_[static_cast<std::size_t>(s)]; t >= 0)
                rec->push(t, postPtr_[static_cast<std::size_t>(s) * 2], postPtr_[static_cast<std::size_t>(s) * 2 + 1], frames);
        if (recMap_[kMaxStrips] >= 0) rec->push(recMap_[kMaxStrips], out[0], out[1], frames);
    }
    cycles_.fetch_add(1, std::memory_order_release);
}

} // namespace maestruc
