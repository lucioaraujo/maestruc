// Maestruc — versão de entrega (streaming) do Master gravado.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Master de entrega (16/11/2026, saida-ref 1.1; o autor: "nivele todos os
// instrumentos para que fiquem preparados para os spotifys da vida"). Do
// master.wav de uma gravação, master_streaming.wav a -14 LUFS / <= -1 dBTP
// (PCM 24 com TPDF, pelo WavWriter do Maestruc) e master_streaming.txt.
// Lê e escreve EM BLOCOS: uma sessão ao vivo de horas não entra na memória.
// As pistas por stem seguem cruas (são para mixar). Fora do fio de áudio.
#pragma once

#include <string>

namespace maestruc {

// `masterWav`: o master.wav escrito pelo WavWriter (PCM 24 estéreo, 44 bytes de
// cabeçalho). Devolve false se não leu ou não escreveu; `resumo` diz o que houve.
bool escreverVersaoDeEntrega(const std::string& masterWav, std::string& resumo);

} // namespace maestruc
