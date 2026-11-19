// Maestruc — os processos dos instrumentos, uma implementação por sistema
// (18/11/2026). O Linux é o código de antes (/proc, posix_spawn com setsid),
// sem mudança de comportamento; macOS pelo libproc e o Mach; Windows pela API
// do sistema (CreateProcess desprendido, Toolhelp, GetProcessTimes, WM_CLOSE).
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "Processos.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <tlhelp32.h>
 #ifdef _MSC_VER
  #pragma comment(lib, "version.lib")
 #endif
#else
 #include <cerrno>
 #include <csignal>
 #include <cstring>
 #include <fcntl.h>
 #include <spawn.h>
 #include <sys/wait.h>
 #include <unistd.h>
 #include <cstdlib>
 #include <dirent.h>
 #include <vector>
 extern char** environ;
 #if JUCE_MAC
  #include <libproc.h>
  #include <mach/mach.h>
  #include <mach/mach_time.h>
  #include <sys/proc.h>        // SZOMB
  #include <sys/proc_info.h>
 #endif
#endif

#include <vector>

namespace maestruc {

namespace {
juce::String u8(const char* t) { return juce::String(juce::CharPointer_UTF8(t)); }

double agoraS() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

void registrarCpu(Processo& p, juce::uint64 ns) {
    const double agora = agoraS();
    if (p.medidoEm > 0.0 && agora > p.medidoEm && ns >= p.cpuNs)
        p.cpu = static_cast<double>(ns - p.cpuNs) / 1.0e9 / (agora - p.medidoEm);
    p.cpuNs = ns;
    p.medidoEm = agora;
}

// Ocupado e total acumulados do sistema (unidades do próprio sistema); a
// fração ocupada entre duas medidas vezes os núcleos dá núcleos em uso.
bool temposSistema(juce::uint64& ocupado, juce::uint64& total);
} // namespace

double cpuSistema() {
    static juce::uint64 ocupado0 = 0, total0 = 0;
    static double em = 0.0, valor = 0.0;
    const double t = agoraS();
    if (em > 0.0 && t - em < 0.5) return valor;
    juce::uint64 ocupado = 0, total = 0;
    if (!temposSistema(ocupado, total)) return valor;
    if (em > 0.0 && total > total0 && ocupado >= ocupado0)
        valor = static_cast<double>(ocupado - ocupado0) / static_cast<double>(total - total0) * juce::SystemStats::getNumCpus();
    ocupado0 = ocupado; total0 = total; em = t;
    return valor;
}

#if JUCE_WINDOWS

namespace {
std::wstring larga(const juce::String& s) { return s.toWideCharPointer(); }

// Regras de aspas do CommandLineToArgvW (barras antes de aspas dobradas).
std::wstring citar(const juce::String& arg) {
    const std::wstring a = larga(arg);
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    size_t barras = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++barras; continue; }
        if (c == L'"') { r.append(barras * 2 + 1, L'\\'); r += L'"'; }
        else { r.append(barras, L'\\'); r += c; }
        barras = 0;
    }
    r.append(barras * 2, L'\\');
    r += L'"';
    return r;
}

juce::String caminhoDoProcesso(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) return {};
    wchar_t buf[2048];
    DWORD tam = 2048;
    juce::String r;
    if (QueryFullProcessImageNameW(h, 0, buf, &tam)) r = juce::String(buf, tam);
    CloseHandle(h);
    return r;
}

BOOL CALLBACK fecharJanela(HWND w, LPARAM pid) {
    DWORD dono = 0;
    GetWindowThreadProcessId(w, &dono);
    if (dono == static_cast<DWORD>(pid) && IsWindowVisible(w)) PostMessageW(w, WM_CLOSE, 0, 0);
    return TRUE;
}
} // namespace

juce::int64 abrirApp(const EntradaCatalogo& e, const juce::StringArray& extras, juce::String& erro) {
    const auto exe = e.programa;
    if (!exe.existsAsFile()) { erro = u8("não instalado"); return 0; }
    std::wstring linha = citar(exe.getFullPathName());
    for (auto& a : e.argsFixos) linha += L" " + citar(a);
    for (auto& a : extras) linha += L" " + citar(a);
    std::vector<wchar_t> mutavel(linha.begin(), linha.end());
    mutavel.push_back(0);
    // Ambiente: o do Maestruc mais as variáveis do catálogo (bloco Unicode).
    std::vector<wchar_t> bloco;
    if (e.ambiente.size() > 0) {
        if (auto* atual = GetEnvironmentStringsW()) {
            for (auto* p = atual; *p != 0; p += wcslen(p) + 1) {
                const std::wstring item(p);
                bool trocada = false;
                for (auto& k : e.ambiente.getAllKeys())
                    if (_wcsnicmp(item.c_str(), (larga(k) + L"=").c_str(), static_cast<size_t>(k.length() + 1)) == 0) trocada = true;
                if (!trocada) bloco.insert(bloco.end(), item.begin(), item.end() + 1);
            }
            FreeEnvironmentStringsW(atual);
        }
        for (auto& k : e.ambiente.getAllKeys()) {
            const auto item = larga(k + "=" + e.ambiente[k]);
            bloco.insert(bloco.end(), item.begin(), item.end());
            bloco.push_back(0);
        }
        bloco.push_back(0);
    }
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    const auto pasta = larga(exe.getParentDirectory().getFullPathName());
    if (!CreateProcessW(larga(exe.getFullPathName()).c_str(), mutavel.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT,
                        bloco.empty() ? nullptr : bloco.data(), pasta.c_str(), &si, &pi)) {
        erro = u8("não abriu: erro ") + juce::String(static_cast<int>(GetLastError()));
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<juce::int64>(pi.dwProcessId);
}

bool lancadorSaiu(juce::int64 pid) { return pid <= 0 || !processoVivo(static_cast<int>(pid)); }

bool processoVivo(int pid) {
    if (pid <= 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr) return false;
    DWORD codigo = 0;
    const bool vivo = GetExitCodeProcess(h, &codigo) && codigo == STILL_ACTIVE;
    CloseHandle(h);
    return vivo;
}

int acharPid(const juce::String& trecho) {
    if (trecho.isEmpty()) return 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe {};
    pe.dwSize = sizeof(pe);
    int achado = 0;
    // primeiro o nome do arquivo (barato, sem abrir o processo); só os que
    // batem têm o caminho inteiro conferido
    const auto nome = trecho.fromLastOccurrenceOf("\\", false, false).fromLastOccurrenceOf("/", false, false);
    for (BOOL ok = Process32FirstW(snap, &pe); ok && achado == 0; ok = Process32NextW(snap, &pe)) {
        if (nome.isNotEmpty() && !juce::String(pe.szExeFile).equalsIgnoreCase(nome)) continue;
        if (caminhoDoProcesso(pe.th32ProcessID).containsIgnoreCase(trecho)) achado = static_cast<int>(pe.th32ProcessID);
    }
    CloseHandle(snap);
    return achado;
}

void medirCpu(Processo& p) {
    if (p.pid <= 0) { p.cpu = 0.0; return; }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(p.pid));
    if (h == nullptr) return;
    FILETIME criado, saiu, nucleo, usuario;
    if (GetProcessTimes(h, &criado, &saiu, &nucleo, &usuario)) {
        const auto v = [](FILETIME f) { return (static_cast<juce::uint64>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        registrarCpu(p, (v(nucleo) + v(usuario)) * 100);   // unidades de 100 ns
    }
    CloseHandle(h);
}

void fecharApp(Processo& p) {
    if (p.pid > 0) EnumWindows(fecharJanela, static_cast<LPARAM>(p.pid));
}

namespace {
bool temposSistema(juce::uint64& ocupado, juce::uint64& total) {
    FILETIME ocioso, nucleo, usuario;   // o tempo de núcleo inclui o ocioso
    if (!GetSystemTimes(&ocioso, &nucleo, &usuario)) return false;
    const auto v = [](FILETIME f) { return (static_cast<juce::uint64>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    total = v(nucleo) + v(usuario);
    ocupado = total - juce::jmin(total, v(ocioso));
    return true;
}
} // namespace

double memoriaLivreGb() {
    MEMORYSTATUSEX m {};
    m.dwLength = sizeof(m);
    return GlobalMemoryStatusEx(&m) ? static_cast<double>(m.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0) : 0.0;
}

juce::String versaoDoExecutavel(const juce::File& exe) {
    const auto caminho = larga(exe.getFullPathName());
    DWORD ignorado = 0;
    const DWORD tam = GetFileVersionInfoSizeW(caminho.c_str(), &ignorado);
    if (tam == 0 || tam > 1024 * 1024) return {};
    std::vector<char> dados(tam);
    if (!GetFileVersionInfoW(caminho.c_str(), 0, tam, dados.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT n = 0;
    if (!VerQueryValueW(dados.data(), L"\\", reinterpret_cast<void**>(&info), &n) || info == nullptr || n < sizeof(*info)) return {};
    return juce::String(HIWORD(info->dwFileVersionMS)) + "." + juce::String(LOWORD(info->dwFileVersionMS)) + "."
           + juce::String(HIWORD(info->dwFileVersionLS));
}

#else // Linux e macOS

#if JUCE_LINUX
// Fecha no filho todo descritor a partir do 3. Com a glibc 2.34+ (o pacote é
// feito no Ubuntu 22.04, glibc 2.35) há o closefrom das ações do spawn; antes
// disso, a lista de /proc/self/fd.
static void fecharHerdados(posix_spawn_file_actions_t& fa) {
   #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);
   #else
    std::vector<int> fds;
    if (DIR* d = opendir("/proc/self/fd")) {
        const int proprio = dirfd(d);
        while (auto* ent = readdir(d)) {
            const int fd = std::atoi(ent->d_name);
            if (fd > 2 && fd != proprio) fds.push_back(fd);
        }
        closedir(d);
    }
    for (int fd : fds) posix_spawn_file_actions_addclose(&fa, fd);
   #endif
}
#endif

juce::int64 abrirApp(const EntradaCatalogo& e, const juce::StringArray& extras, juce::String& erro) {
    const auto exe = e.programa;
    if (!exe.existsAsFile()) { erro = u8("não instalado"); return 0; }
    const auto logs = pastaLogs();
    logs.createDirectory();
    const auto log = logs.getChildFile(nomeDoLog(e));

    juce::StringArray args;
    args.add(exe.getFullPathName());
    args.addArray(e.argsFixos);
    args.addArray(extras);
    std::vector<std::string> store;
    for (auto& a : args) store.push_back(a.toStdString());
    std::vector<char*> argv;
    for (auto& s : store) argv.push_back(s.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, log.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
   #if JUCE_LINUX
    posix_spawn_file_actions_addchdir_np(&fa, exe.getParentDirectory().getFullPathName().toRawUTF8());
    // O filho não herda nada além de 0, 1 e 2 (18/11/2026): herdava o socket
    // do canal (porta 47900), e um instrumento aberto segurava a porta depois
    // que o Maestruc fechava; o Maestruc seguinte ficava surdo.
    fecharHerdados(fa);
   #endif
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    short flags = 0;
   #ifdef POSIX_SPAWN_SETSID
    flags |= POSIX_SPAWN_SETSID;
   #endif
   #ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;   // macOS: o mesmo, pelo sistema
   #endif
    posix_spawnattr_setflags(&at, flags);
    // Ambiente: o do Maestruc mais as variáveis do catálogo (substituem).
    std::vector<std::string> ambStore;
    for (char** p = environ; p != nullptr && *p != nullptr; ++p) {
        const std::string item(*p);
        bool trocada = false;
        for (auto& k : e.ambiente.getAllKeys())
            if (item.rfind(k.toStdString() + "=", 0) == 0) trocada = true;
        if (!trocada) ambStore.push_back(item);
    }
    for (auto& k : e.ambiente.getAllKeys()) ambStore.push_back((k + "=" + e.ambiente[k]).toStdString());
    std::vector<char*> envp;
    for (auto& s : ambStore) envp.push_back(s.data());
    envp.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, argv[0], &fa, &at, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (rc != 0) { erro = u8("não abriu: ") + juce::String(std::strerror(rc)); return 0; }
    return pid;
}

bool lancadorSaiu(juce::int64 pid) {
    if (pid <= 0) return true;
    int st = 0;
    return waitpid(static_cast<pid_t>(pid), &st, WNOHANG) == static_cast<pid_t>(pid);
}

#if JUCE_LINUX

bool processoVivo(int pid) {
    return pid > 0 && juce::File("/proc/" + juce::String(pid)).isDirectory()
           && !juce::File("/proc/" + juce::String(pid) + "/stat").loadFileAsString()
                   .fromLastOccurrenceOf(")", false, false).trimStart().startsWithChar('Z');
}

int acharPid(const juce::String& trecho) {
    if (trecho.isEmpty()) return 0;
    for (const auto& entry : juce::RangedDirectoryIterator(juce::File("/proc"), false, "*", juce::File::findDirectories)) {
        const auto name = entry.getFile().getFileName();
        if (!name.containsOnly("0123456789")) continue;
        const auto exe = entry.getFile().getChildFile("exe").getLinkedTarget().getFullPathName();
        if (exe.contains(trecho) && !exe.endsWith(".sh") && !exe.contains("/bash"))
            return name.getIntValue();
    }
    return 0;
}

void medirCpu(Processo& p) {
    if (p.pid <= 0) { p.cpu = 0.0; return; }
    const auto stat = juce::File("/proc/" + juce::String(p.pid) + "/stat").loadFileAsString();
    const auto campos = juce::StringArray::fromTokens(stat.fromLastOccurrenceOf(")", false, false), " ", "");
    if (campos.size() < 14) return;
    const auto ticks = static_cast<juce::uint64>(campos[11].getLargeIntValue() + campos[12].getLargeIntValue());
    registrarCpu(p, ticks * 1000000000ULL / static_cast<juce::uint64>(sysconf(_SC_CLK_TCK)));
}

namespace {
bool temposSistema(juce::uint64& ocupado, juce::uint64& total) {
    // 1ª linha de /proc/stat: user nice system idle iowait irq softirq steal
    const auto linha = juce::File("/proc/stat").loadFileAsString().upToFirstOccurrenceOf("\n", false, false);
    const auto c = juce::StringArray::fromTokens(linha, " ", "");
    if (c.size() < 6 || c[0] != "cpu") return false;
    total = 0;
    for (int i = 1; i < juce::jmin(c.size(), 9); ++i) total += static_cast<juce::uint64>(c[i].getLargeIntValue());
    const auto ocioso = static_cast<juce::uint64>(c[4].getLargeIntValue() + c[5].getLargeIntValue());
    ocupado = total - juce::jmin(total, ocioso);
    return true;
}
} // namespace

double memoriaLivreGb() {
    const auto info = juce::File("/proc/meminfo").loadFileAsString();
    const auto linha = info.fromFirstOccurrenceOf("MemAvailable:", false, false).upToFirstOccurrenceOf("\n", false, false);
    return linha.trim().getLargeIntValue() / 1024.0 / 1024.0;
}

#else // JUCE_MAC

bool processoVivo(int pid) {
    if (pid <= 0) return false;
    proc_bsdinfo info {};
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != static_cast<int>(sizeof(info)))
        return ::kill(pid, 0) == 0 || errno == EPERM;
    return info.pbi_status != SZOMB;
}

int acharPid(const juce::String& trecho) {
    if (trecho.isEmpty()) return 0;
    std::vector<pid_t> pids(4096);
    const int n = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    char caminho[PROC_PIDPATHINFO_MAXSIZE];
    for (int i = 0; i < n && i < static_cast<int>(pids.size()); ++i) {
        if (pids[static_cast<size_t>(i)] <= 0) continue;
        if (proc_pidpath(pids[static_cast<size_t>(i)], caminho, sizeof(caminho)) <= 0) continue;
        const auto exe = juce::String(juce::CharPointer_UTF8(caminho));
        if (exe.contains(trecho) && !exe.endsWith(".sh") && !exe.contains("/bash")) return pids[static_cast<size_t>(i)];
    }
    return 0;
}

void medirCpu(Processo& p) {
    if (p.pid <= 0) { p.cpu = 0.0; return; }
    proc_taskinfo ti {};
    if (proc_pidinfo(p.pid, PROC_PIDTASKINFO, 0, &ti, sizeof(ti)) != static_cast<int>(sizeof(ti))) return;
    // pti_total_* vem em unidades do relógio Mach (ns no Intel, não no Apple Silicon)
    mach_timebase_info_data_t base {};
    mach_timebase_info(&base);
    const auto ticks = static_cast<juce::uint64>(ti.pti_total_user + ti.pti_total_system);
    registrarCpu(p, base.denom != 0 ? ticks * base.numer / base.denom : ticks);
}

namespace {
bool temposSistema(juce::uint64& ocupado, juce::uint64& total) {
    host_cpu_load_info_data_t c {};
    mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&c), &n) != KERN_SUCCESS) return false;
    ocupado = static_cast<juce::uint64>(c.cpu_ticks[CPU_STATE_USER]) + c.cpu_ticks[CPU_STATE_SYSTEM] + c.cpu_ticks[CPU_STATE_NICE];
    total = ocupado + c.cpu_ticks[CPU_STATE_IDLE];
    return true;
}
} // namespace

double memoriaLivreGb() {
    vm_statistics64_data_t vm {};
    mach_msg_type_number_t n = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &n) != KERN_SUCCESS) return 0.0;
    const double pagina = static_cast<double>(vm_kernel_page_size);
    return (static_cast<double>(vm.free_count) + static_cast<double>(vm.inactive_count)) * pagina / (1024.0 * 1024.0 * 1024.0);
}

#endif

void fecharApp(Processo& p) {
    if (p.pid > 0) ::kill(p.pid, SIGTERM);
}

juce::String versaoDoExecutavel(const juce::File&) { return {}; }

#endif

} // namespace maestruc
