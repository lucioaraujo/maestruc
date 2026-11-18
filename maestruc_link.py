#!/usr/bin/env python3
"""Maestruc Link (maestruc.link/1): registro, comandos e escuta por OSC local.

Só a biblioteca padrão. Lado do Maestruc (`reger`) e um instrumento simulado
(`simular`) para ensaiar o protocolo sem abrir nenhum app. Especificação em
PROTOCOLO_MAESTRUC_LINK.md.
"""

from __future__ import annotations

import argparse
import json
import os
import secrets
import select
import shutil
import socket
import struct
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
MAESTRUC_PORT = 47900
PREFIX = "/rasgo/1/"
MAX_PACKET = 1024
ALIVE_TIMEOUT = 3.0


# ---------------- OSC 1.0 (i, f, s) ----------------

def _pad(data: bytes) -> bytes:
    return data + b"\0" * (4 - len(data) % 4)


def encode(address: str, *args) -> bytes:
    tags, payload = ",", b""
    for value in args:
        if isinstance(value, bool):
            value = int(value)
        if isinstance(value, int):
            tags += "i"
            payload += struct.pack(">i", value)
        elif isinstance(value, float):
            tags += "f"
            payload += struct.pack(">f", value)
        elif isinstance(value, str):
            tags += "s"
            payload += _pad(value.encode("utf-8"))
        else:
            raise TypeError(f"tipo OSC não suportado: {type(value).__name__}")
    packet = _pad(address.encode("utf-8")) + _pad(tags.encode("ascii")) + payload
    if len(packet) > MAX_PACKET:
        raise ValueError("mensagem acima de 1024 bytes")
    return packet


def _read_string(data: bytes, offset: int) -> tuple[str, int]:
    end = data.index(b"\0", offset)
    text = data[offset:end].decode("utf-8")
    return text, (end // 4 + 1) * 4


def decode(data: bytes) -> tuple[str, list]:
    if len(data) > MAX_PACKET or len(data) % 4:
        raise ValueError("pacote OSC inválido")
    address, offset = _read_string(data, 0)
    tags, offset = _read_string(data, offset)
    if not address.startswith("/") or not tags.startswith(","):
        raise ValueError("pacote OSC inválido")
    args = []
    for tag in tags[1:]:
        if tag == "i":
            args.append(struct.unpack_from(">i", data, offset)[0])
            offset += 4
        elif tag == "f":
            args.append(struct.unpack_from(">f", data, offset)[0])
            offset += 4
        elif tag == "s":
            text, offset = _read_string(data, offset)
            args.append(text)
        else:
            raise ValueError(f"tipo OSC não suportado: {tag}")
    return address, args


def now_us() -> str:
    return str(time.time_ns() // 1000)


# ---------------- perfil da máquina (§5) ----------------

def machine_profile(record_dir: Path) -> dict:
    available_kb = None
    try:
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                available_kb = int(line.split()[1])
    except OSError:
        pass
    free = shutil.disk_usage(record_dir).free
    return {
        "nucleos": os.cpu_count() or 1,
        "ramDisponivelMiB": None if available_kb is None else available_kb // 1024,
        "discoGravacao": str(record_dir),
        "discoLivreGiB": round(free / 1024**3, 1),
        "carga1min": round(os.getloadavg()[0], 2) if hasattr(os, "getloadavg") else None,
    }


def process_ticks(pid: int) -> int | None:
    """utime + stime do processo, em ticks (Linux); None se não houver."""
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])
    except (OSError, IndexError, ValueError):
        return None


TICKS = os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100


def admission(profile: dict, loads: dict[int, float], xruns_recent: bool) -> tuple[bool, str]:
    """Regra inicial de admissão (§5.4): folga de CPU e nenhum xrun recente.

    `loads` em núcleos por instrumento (CPU do processo inteiro, interface
    incluída). O recém-chegado conta como um núcleo até ser medido."""
    cores = profile["nucleos"]
    used = sum(loads.values())
    if xruns_recent:
        return False, "houve atrasos de áudio nos últimos 30 s"
    if used + 1.0 > 0.70 * cores:
        return False, f"carga {used:.2f} de {cores} núcleos; sem folga para mais um"
    return True, f"carga {used:.2f} de {cores} núcleos"


# ---------------- senha da sessão (15/11/2026) ----------------

def bridges_dir() -> Path | None:
    """A pasta das pontes de som e da senha, igual à de receptor/PonteDeSom.h:
    $TMPDIR (ou /tmp)/rasgo-maestruc-<uid>, só do usuário."""
    base = os.environ.get("TMPDIR") or "/tmp"
    folder = Path(base.rstrip("/") or "/") / f"rasgo-maestruc-{os.getuid()}"
    try:
        folder.mkdir(mode=0o700, exist_ok=True)
        st = folder.lstat()
    except OSError:
        return None
    import stat as _stat
    if not _stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077:
        return None
    return folder


def write_session_token(token: str) -> Path | None:
    """Grava a senha só para o usuário (0600), trocando o arquivo de uma vez."""
    folder = bridges_dir()
    if folder is None:
        return None
    target = folder / "sessao.txt"
    temp = folder / f".sessao-{os.getpid()}.tmp"
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(token + "\n")
    os.replace(temp, target)
    return target


def read_session_token() -> str | None:
    folder = bridges_dir()
    if folder is None:
        return None
    path = folder / "sessao.txt"
    try:
        st = path.lstat()
        import stat as _stat
        if not _stat.S_ISREG(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077:
            return None
        token = path.read_text().split("\n", 1)[0]
    except OSError:
        return None
    return token if 16 <= len(token) <= 128 else None


# ---------------- Maestruc ----------------

class Maestruc:
    def __init__(self, port: int = MAESTRUC_PORT, session: str | None = None):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((HOST, port))
        self.port = self.sock.getsockname()[1]
        # senha nova a cada sessão, num arquivo só do usuário (o receptor a confere)
        self.session = session or secrets.token_hex(16)
        if write_session_token(self.session) is None:
            raise OSError("pasta das pontes indisponível (não é só do usuário?)")
        self.instruments: dict[int, dict] = {}
        self.log: list[dict] = []

    def close(self) -> None:
        self.sock.close()

    def send(self, port: int, address: str, *args) -> None:
        # todo comando leva a senha por último (welcome e who não)
        if address not in ("welcome", "who"):
            args = (*args, self.session)
        self.sock.sendto(encode(PREFIX + address, *args), (HOST, port))

    def broadcast(self, address: str, *args) -> None:
        for port in list(self.instruments):
            self.send(port, address, *args)

    def poll(self, seconds: float) -> None:
        deadline = time.monotonic() + seconds
        while (left := deadline - time.monotonic()) > 0:
            ready, _, _ = select.select([self.sock], [], [], min(left, 0.2))
            if ready:
                data, (host, _) = self.sock.recvfrom(MAX_PACKET + 1)
                if host == HOST:
                    try:
                        self.handle(*decode(data))
                    except (ValueError, struct.error, UnicodeDecodeError):
                        pass
            self.expire()

    def expire(self) -> None:
        moment = time.monotonic()
        limit = moment - ALIVE_TIMEOUT
        for info in self.instruments.values():
            ticks, at = info["amostra"]
            if ticks is not None and moment - at >= 1.0:
                current = process_ticks(info["pid"])
                if current is not None:
                    info["cpu"] = (current - ticks) / TICKS / (moment - at)
                info["amostra"] = (current, moment)
        for port, info in list(self.instruments.items()):
            if info["visto"] < limit:
                self.note("ausente", port, info["nome"])
                del self.instruments[port]

    def note(self, kind: str, port: int, *details) -> None:
        self.log.append({"t": now_us(), "tipo": kind, "porta": port, "dados": list(details)})

    def handle(self, address: str, args: list) -> None:
        if not address.startswith(PREFIX):
            return
        name = address[len(PREFIX):]
        if name == "hello" and len(args) == 5:
            nome, versao, port, pid, caps = args
            if not (1024 <= port <= 65535):
                return
            if port in self.instruments:      # hello repetido antes do welcome chegar
                self.instruments[port]["visto"] = time.monotonic()
            else:
                self.instruments[port] = {
                    "nome": nome, "versao": versao, "pid": pid,
                    "caps": set(filter(None, caps.split(","))),
                    "visto": time.monotonic(), "carga": 0.0, "atrasos": 0,
                    "cpu": None, "amostra": (process_ticks(pid), time.monotonic()),
                }
                self.note("hello", port, nome, versao, pid, caps)
            self.send(port, "welcome", self.session)
            return
        if name == "alive" and args and isinstance(args[0], int) and args[0] not in self.instruments:
            if 1024 <= args[0] <= 65535:
                self.send(args[0], "who")      # Maestruc reaberto: pedir novo hello
            return
        if not args or not isinstance(args[0], int) or args[0] not in self.instruments:
            return
        port, info = args[0], self.instruments[args[0]]
        info["visto"] = time.monotonic()
        if name == "bye":
            self.note("bye", port, info["nome"])
            del self.instruments[port]
        elif name == "load" and len(args) == 3:
            info["carga"], info["atrasos"] = float(args[1]), int(args[2])
        elif name in ("ack", "state", "event", "obs", "mixer/canal", "mixer/fim"):
            self.note(name, port, *args[1:])
        elif name == "ponte" and len(args) == 5 and args[4] == self.session:
            info["ponte"] = {"caminho": args[1], "pares": args[2], "taxa": args[3]}
            info["caps"].add("audio")
            self.note("ponte", port, *args[1:4])

    def loads(self) -> dict[int, float]:
        """Núcleos usados por instrumento: CPU medida do processo; sem medida,
        a carga declarada do callback de áudio (subestima a interface)."""
        return {port: info["cpu"] if info["cpu"] is not None else info["carga"]
                for port, info in self.instruments.items()}


# ---------------- instrumento simulado ----------------

class SimulatedInstrument:
    """Faz o papel de um app: anuncia, responde e emite acontecimentos."""

    def __init__(self, name: str, caps: str, maestruc_port: int = MAESTRUC_PORT):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((HOST, 0))
        self.port = self.sock.getsockname()[1]
        self.name, self.caps = name, set(filter(None, caps.split(",")))
        self.maestruc = (HOST, maestruc_port)
        self.welcomed = False
        self.playing, self.seed, self.bpm = False, "00000000", 120.0
        self.received: list[tuple[str, list]] = []

    def close(self) -> None:
        self.out("bye", self.port)
        self.sock.close()

    def out(self, address: str, *args) -> None:
        self.sock.sendto(encode(PREFIX + address, *args), self.maestruc)

    def hello(self) -> None:
        self.out("hello", self.name, "sim", self.port, os.getpid(), ",".join(sorted(self.caps)))

    def capability_for(self, name: str, args: list) -> str | None:
        if name.startswith("transport/"):
            return "transport"
        if name.startswith("rec/"):
            return "rec"
        if name == "macro" and args:
            return f"macro:{args[0]}"
        return name

    def poll(self, seconds: float) -> None:
        deadline = time.monotonic() + seconds
        last_alive = 0.0
        while (left := deadline - time.monotonic()) > 0:
            if time.monotonic() - last_alive >= 1.0:
                self.hello() if not self.welcomed else self.out("alive", self.port)
                self.out("load", self.port, 0.2, 0)
                last_alive = time.monotonic()
            ready, _, _ = select.select([self.sock], [], [], min(left, 0.2))
            if not ready:
                continue
            data, _ = self.sock.recvfrom(MAX_PACKET + 1)
            try:
                address, args = decode(data)
            except (ValueError, struct.error, UnicodeDecodeError):
                continue
            if not address.startswith(PREFIX):
                continue
            name = address[len(PREFIX):]
            self.received.append((name, args))
            if name == "welcome":
                token = read_session_token()
                if token and args and args[0] == token:
                    self.welcomed, self.token = True, token
                continue
            if name == "who":
                self.welcomed = False
                self.hello()
                continue
            # todo comando traz a senha por último
            if not getattr(self, "token", None) or not args or args[-1] != self.token:
                continue
            args = args[:-1]
            if name == "query":
                self.out("state", self.port, int(self.playing), self.seed, self.bpm)
                continue
            needed = self.capability_for(name, args)
            ok = needed in self.caps
            if ok:
                if name == "transport/play":
                    self.playing = True
                    self.out("event", self.port, "ataque", 1.0, now_us())
                elif name == "transport/stop":
                    self.playing = False
                elif name == "seed":
                    self.seed = args[0]
                elif name == "open":
                    ok = bool(args) and Path(args[0]).is_absolute() and Path(args[0]).is_file()
                    self.out("ack", self.port, address, int(ok), "" if ok else "arquivo inexistente")
                    continue
                elif name == "tempo":
                    self.bpm = float(args[0])
            self.out("ack", self.port, address, int(ok), "" if ok else "capacidade ausente")


# ---------------- linha de comando ----------------

COMMANDS = {
    "play": ("transport/play", lambda a: [a or "0"]),
    "stop": ("transport/stop", lambda a: []),
    "tempo": ("tempo", lambda a: [float(a)]),
    "seed": ("seed", lambda a: [a]),
    "abrir": ("open", lambda a: [str(Path(a).expanduser().resolve())]),
    "secao": ("section", lambda a: [int(a.split(":", 1)[0]), a.split(":", 1)[1]]),
    "tom": ("key", lambda a: [int(a.split(":", 1)[0]), a.split(":", 1)[1]]),
    "intensidade": ("macro", lambda a: ["intensidade", float(a)]),
    "densidade": ("macro", lambda a: ["densidade", float(a)]),
    "orcamento": ("budget", lambda a: [int(a)]),
    "estado": ("query", lambda a: []),
    # mixer de dentro do app: índice,valor (índice -1 = master)
    "mixer-nivel": ("mixer/nivel", lambda a: [int(a.split(",")[0]), float(a.split(",")[1])]),
    "mixer-mudo": ("mixer/mudo", lambda a: [int(a.split(",")[0]), int(a.split(",")[1])]),
    "mixer-solo": ("mixer/solo", lambda a: [int(a.split(",")[0]), int(a.split(",")[1])]),
    "mixer-listar": ("mixer/listar", lambda a: []),
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Maestruc Link: reger instrumentos RASGO por OSC local")
    sub = parser.add_subparsers(dest="command", required=True)
    reger = sub.add_parser("reger", help="registrar instrumentos, enviar comandos e gravar o registro")
    reger.add_argument("--segundos", type=float, default=10.0, help="duração total (máx. 3600)")
    reger.add_argument("--comando", action="append", default=[],
                       help="ORDEM:SEGUNDO, ex.: play:2, tempo=96:1, secao=1:B:5 (ver COMMANDS)")
    reger.add_argument("--registro", type=Path, help="gravar o registro em JSON")
    reger.add_argument("--disco", type=Path, default=Path.cwd(), help="disco onde se gravaria")
    sim = sub.add_parser("simular", help="instrumento simulado, para ensaiar sem abrir apps")
    sim.add_argument("--nome", default="simulado")
    sim.add_argument("--caps", default="transport,tempo,seed,event,load")
    sim.add_argument("--segundos", type=float, default=10.0)
    args = parser.parse_args()

    if args.command == "simular":
        instrument = SimulatedInstrument(args.nome, args.caps)
        try:
            instrument.poll(min(args.segundos, 3600))
        finally:
            instrument.close()
        for name, values in instrument.received:
            print(name, values)
        return 0

    total = min(args.segundos, 3600)
    schedule = []
    for raw in args.comando:
        order, _, at = raw.rpartition(":")
        word, _, value = order.partition("=")
        if word not in COMMANDS:
            raise ValueError(f"comando desconhecido: {word}")
        schedule.append((float(at), word, value))
    schedule.sort()
    maestruc = Maestruc()
    profile = machine_profile(args.disco)
    print("perfil:", json.dumps(profile, ensure_ascii=False))
    started = time.monotonic()
    try:
        for at, word, value in schedule + [(total, None, None)]:
            maestruc.poll(max(0.0, started + at - time.monotonic()))
            if word is None:
                break
            address, build = COMMANDS[word]
            maestruc.broadcast(address, *build(value))
            print(f"{at:6.1f} s  {word} {value} → {len(maestruc.instruments)} instrumento(s)")
    finally:
        maestruc.close()
    for port, info in maestruc.instruments.items():
        cpu = "sem medida" if info["cpu"] is None else f"{info['cpu']:.2f} núcleo"
        print(f"{info['nome']} (porta {port}): CPU {cpu}; áudio {info['carga']:.2f}; atrasos {info['atrasos']}")
    for item in maestruc.log:
        print(item["tipo"], item["porta"], item["dados"])
    ok, reason = admission(profile, maestruc.loads(), any(i["atrasos"] for i in maestruc.instruments.values()))
    print("admissão de mais um instrumento:", "sim" if ok else "não", "—", reason)
    if args.registro:
        args.registro.write_text(json.dumps({"schemaVersion": "maestruc.link-log/1", "perfil": profile,
                                             "registro": maestruc.log}, ensure_ascii=False, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"maestruc-link: {error}", file=sys.stderr)
        raise SystemExit(2) from error
