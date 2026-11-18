#!/usr/bin/env python3
"""Ensaios do maestruc_link.py: OSC, registro, comandos, ausência e admissão."""

import os
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

# Os ensaios usam uma pasta de pontes e senha só deles (TMPDIR próprio), para
# nunca trocar a senha de um Maestruc que esteja aberto na máquina.
os.environ["TMPDIR"] = tempfile.mkdtemp(prefix="maestruc-ensaio-")

import maestruc_link as ml


class OscTests(unittest.TestCase):
    def test_roundtrip(self):
        packet = ml.encode("/rasgo/1/hello", "Batruc", "0.1.2", 50000, 4242, "transport,seed")
        self.assertEqual(len(packet) % 4, 0)
        self.assertEqual(ml.decode(packet), ("/rasgo/1/hello", ["Batruc", "0.1.2", 50000, 4242, "transport,seed"]))

    def test_float_and_utf8(self):
        address, args = ml.decode(ml.encode("/rasgo/1/section", 2, "clímax", 0.5))
        self.assertEqual(args[:2], [2, "clímax"])
        self.assertAlmostEqual(args[2], 0.5)

    def test_rejects_garbage_and_oversize(self):
        with self.assertRaises(ValueError):
            ml.decode(b"abc")
        with self.assertRaises(ValueError):
            ml.encode("/x", "a" * 2000)


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.maestruc = ml.Maestruc(port=0)

    def tearDown(self):
        self.maestruc.close()

    def run_instrument(self, instrument, seconds):
        thread = threading.Thread(target=instrument.poll, args=(seconds,))
        thread.start()
        return thread

    def test_register_command_ack_and_event(self):
        sim = ml.SimulatedInstrument("ensaio", "transport,tempo,seed,event,load", self.maestruc.port)
        thread = self.run_instrument(sim, 3.0)
        self.maestruc.poll(1.3)
        self.assertIn(sim.port, self.maestruc.instruments)
        self.assertEqual(self.maestruc.instruments[sim.port]["pid"], __import__("os").getpid())
        self.maestruc.broadcast("tempo", 96.0)
        self.maestruc.broadcast("seed", "1a2b3c4d")
        self.maestruc.broadcast("transport/play", "0")
        self.maestruc.broadcast("key", 2, "dorico")   # capacidade ausente
        self.maestruc.broadcast("query")
        self.maestruc.poll(1.0)
        thread.join()
        sim.close()
        kinds = [(i["tipo"], i["dados"][:2]) for i in self.maestruc.log]
        self.assertIn(("ack", ["/rasgo/1/tempo", 1]), kinds)
        self.assertIn(("ack", ["/rasgo/1/key", 0]), kinds)
        self.assertTrue(any(k == "event" and d[0] == "ataque" for k, d in kinds))
        state = next(i["dados"] for i in self.maestruc.log if i["tipo"] == "state")
        self.assertEqual(state[:2], [1, "1a2b3c4d"])
        self.assertAlmostEqual(state[2], 96.0)

    def test_open_piece(self):
        import tempfile
        sim = ml.SimulatedInstrument("ensaio", "open", self.maestruc.port)
        thread = self.run_instrument(sim, 2.5)
        self.maestruc.poll(1.3)
        with tempfile.NamedTemporaryFile(suffix=".batruc.txt") as piece:
            self.maestruc.broadcast("open", piece.name)
            self.maestruc.broadcast("open", "/nao/existe.batruc.txt")
            self.maestruc.poll(0.8)
        thread.join()
        sim.close()
        acks = [i["dados"] for i in self.maestruc.log if i["tipo"] == "ack"]
        self.assertIn(["/rasgo/1/open", 1], [a[:2] for a in acks])
        self.assertTrue(any(a[:2] == ["/rasgo/1/open", 0] and a[2] == "arquivo inexistente" for a in acks))

    def test_absence_after_silence(self):
        sim = ml.SimulatedInstrument("some", "transport", self.maestruc.port)
        thread = self.run_instrument(sim, 1.2)
        self.maestruc.poll(1.0)
        self.assertIn(sim.port, self.maestruc.instruments)
        thread.join()
        sim.sock.close()           # cai sem bye
        self.maestruc.poll(ml.ALIVE_TIMEOUT + 0.5)
        self.assertNotIn(sim.port, self.maestruc.instruments)
        self.assertTrue(any(i["tipo"] == "ausente" for i in self.maestruc.log))


PROBE = Path(__file__).resolve().parent / ".local-build" / "link_probe"


@unittest.skipUnless(PROBE.is_file(), "compile antes: receptor/teste/compilar.sh")
class JuceReceptorTests(unittest.TestCase):
    """O receptor C++ (MaestrucLink.h) conversando com o Maestruc em Python."""

    def test_probe(self):
        maestruc = ml.Maestruc(port=0)
        try:
            probe = subprocess.Popen([str(PROBE), "4", str(maestruc.port)], stdout=subprocess.PIPE, text=True)
            maestruc.poll(1.5)
            self.assertEqual(len(maestruc.instruments), 1)
            port, info = next(iter(maestruc.instruments.items()))
            self.assertEqual(info["nome"], "Sonda")
            self.assertEqual(info["pid"], probe.pid)
            self.assertEqual(info["caps"], {"event", "load", "transport", "tempo", "seed", "open", "macro:intensidade",
                                            "mixer", "audio"})
            # senha errada: o instrumento ignora o comando (nenhum ack)
            maestruc.sock.sendto(ml.encode(ml.PREFIX + "tempo", 50.0, "senha-errada"), (ml.HOST, port))
            import tempfile
            piece = tempfile.NamedTemporaryFile(suffix=".batruc.txt")
            other = tempfile.NamedTemporaryFile(suffix=".wav")
            maestruc.broadcast("open", piece.name)
            maestruc.broadcast("open", other.name)            # o instrumento recusa
            maestruc.broadcast("tempo", 96.0)
            maestruc.broadcast("seed", "1a2b3c4d")
            maestruc.broadcast("transport/play", "0")
            maestruc.broadcast("macro", "intensidade", 1.7)   # limitado a 1
            maestruc.broadcast("macro", "brilho", 0.5)        # não declarada
            maestruc.broadcast("key", 2, "dorico")            # sem handler
            maestruc.broadcast("mixer/nivel", 0, -12.0)
            maestruc.broadcast("mixer/mudo", 1, 1)
            maestruc.broadcast("mixer/nivel", 7, -1.0)          # canal inexistente
            maestruc.broadcast("mixer/nivel", 0, 99.0)          # fora da faixa
            maestruc.broadcast("query")
            maestruc.poll(1.5)
            ponte = maestruc.instruments[port].get("ponte")
            output, _ = probe.communicate(timeout=10)
        finally:
            maestruc.close()
        acks = {i["dados"][0]: i["dados"][1] for i in maestruc.log if i["tipo"] == "ack"}
        opens = [i["dados"][1:] for i in maestruc.log if i["tipo"] == "ack" and i["dados"][0] == "/rasgo/1/open"]
        self.assertEqual(opens, [[1, ""], [0, "formato desconhecido"]])
        self.assertIn("open " + piece.name, output)
        piece.close(); other.close()
        self.assertEqual(acks["/rasgo/1/tempo"], 1)
        self.assertNotIn("tempo 50.0", output)                       # a senha errada não passou
        # ponte de som anunciada, dentro da pasta das pontes, já removida ao fechar
        self.assertIsNotNone(ponte)
        self.assertEqual(ponte["pares"], 2)
        self.assertTrue(ponte["caminho"].startswith(str(ml.bridges_dir())))
        self.assertFalse(Path(ponte["caminho"]).exists())
        # mixer: lista com master e dois canais; nível e mudo aplicados; recusas
        canais = [i["dados"] for i in maestruc.log if i["tipo"] == "mixer/canal"]
        # 16/11/2026: cada canal diz os próprios objetos e a faixa do fader
        self.assertIn([-1, "master", 0.0, 0, 0, "nivel,mudo", -60.0, 6.0], canais)
        self.assertIn([0, "bumbo", -3.0, 0, 0, "nivel,mudo,solo", -90.0, 12.0], canais)
        self.assertIn([-1, "master", 0.0, 0, 0], [c[:5] for c in canais])
        self.assertIn("mixer 0 nivel -12.0", output)
        self.assertIn("mixer 1 mudo 1.0", output)
        mixer_acks = [i["dados"][:3] for i in maestruc.log if i["tipo"] == "ack" and i["dados"][0] == "/rasgo/1/mixer/nivel"]
        self.assertIn(["/rasgo/1/mixer/nivel", 0, "canal ou valor recusado"], mixer_acks)
        self.assertEqual(sum(1 for a in mixer_acks if a[1] == 1), 1)
        self.assertEqual(acks["/rasgo/1/key"], 0)
        state = next(i["dados"] for i in maestruc.log if i["tipo"] == "state")
        self.assertEqual(state[:2], [1, "1a2b3c4d"])
        self.assertTrue(any(i["tipo"] == "event" and i["dados"][0] == "ataque" for i in maestruc.log))
        self.assertAlmostEqual(maestruc.instruments[port]["carga"], 0.25, places=3)
        self.assertIn("macro intensidade 1.00", output)
        self.assertIn("conectado=1", output)
        self.assertEqual(probe.returncode, 0)


class AdmissionTests(unittest.TestCase):
    profile = {"nucleos": 4}

    def test_room(self):
        self.assertTrue(ml.admission(self.profile, {1: 0.7, 2: 0.5}, False)[0])

    def test_no_room(self):
        self.assertFalse(ml.admission(self.profile, {1: 0.9, 2: 0.8, 3: 0.6}, False)[0])

    def test_xruns_block(self):
        self.assertFalse(ml.admission(self.profile, {1: 0.1}, True)[0])


if __name__ == "__main__":
    unittest.main()
