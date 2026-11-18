// Maestruc — línguas da janela: português (fonte), inglês, espanhol, francês.
// A chave é o texto em português; o que não estiver na tabela sai em
// português. Textos longos (APRENDER, SOBRE) estão em Textos.h.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <string>
#include <unordered_map>

namespace maestruc {

enum class Lingua { pt = 0, en = 1, es = 2, fr = 3 };

inline Lingua& linguaAtual() {
    static Lingua l = Lingua::pt;
    return l;
}

const std::unordered_map<std::string, std::array<const char*, 3>>& tabelaTraducoes();

// Texto visível na língua escolhida (sempre UTF-8 explícito).
inline juce::String T(const char* pt) {
    const auto l = linguaAtual();
    if (l != Lingua::pt) {
        const auto& t = tabelaTraducoes();
        if (auto it = t.find(pt); it != t.end())
            return juce::String(juce::CharPointer_UTF8(it->second[static_cast<std::size_t>(l) - 1]));
    }
    return juce::String(juce::CharPointer_UTF8(pt));
}
inline juce::String T(const juce::String& pt) { return T(pt.toRawUTF8()); }

} // namespace maestruc
