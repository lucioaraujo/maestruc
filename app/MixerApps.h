// Maestruc — página MIXERS (16/11/2026): o mixer de dentro de cada app, no
// visual do Maestruc, com os mesmos objetos do mixer do instrumento.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
//
// Decisão do autor (16/11/2026): "os mixers no maestruc terão o layout do
// maestruc, porém com os mesmos objetos dos mixers de cada instrumento, pode
// escolher o melhor jeito de visualizar no maestruc".
//
// O autor: "cada instrumento ao ser selecionado dentro do maestruc irá mostrar
// seu mixer próprio - assim além do maestruc ter um controle de mixer entre
// todas as saídas dos instrumentos, também será capaz de possibilitar ao
// usuário que tenha controle fino dos stems no mixer de cada instrumento".
//
// O instrumento escolhido (botão MIXER na faixa dele em SAÍDAS, ou a fileira
// de instrumentos no alto da página) ocupa a página: cada canal do mixer dele
// é uma faixa no desenho das faixas de SAÍDAS (fader, M, S), mas só com os
// controles que o canal tem de verdade no instrumento e na faixa de dB dele
// (o app os anuncia em mixer/canal). O master do app (índice −1) fica à
// direita, no desenho do master. Quem toca no app continua mandando: o que
// muda lá volta para cá.
#pragma once

#include "Componentes.h"
#include "Orquestra.h"

#include <functional>
#include <map>

namespace maestruc {

// Um canal do mixer de um app.
class CanalApp final : public juce::Component {
public:
    std::function<void(float)> onNivel;
    std::function<void(bool)> onMudo, onSolo;

    CanalApp() {
        addChildComponent(fader_);
        fader_.onChange = [this](float db) { if (onNivel) onNivel(db); repaint(); };
        for (auto* b : { &mudo_, &solo_ }) {
            addChildComponent(*b);
            b->setClickingTogglesState(true);
            estiloBotao(*b, cor::painel, cor::tinta, cor::bordaControle);
            b->getProperties().set("letra", 12.0f);
        }
        mudo_.setButtonText("M");
        solo_.setButtonText("S");
        mudo_.setColour(juce::TextButton::buttonOnColourId, cor::pausa);
        solo_.setColour(juce::TextButton::buttonOnColourId, cor::tagMusica);
        mudo_.onClick = [this] { if (onMudo) onMudo(mudo_.getToggleState()); };
        solo_.onClick = [this] { if (onSolo) onSolo(solo_.getToggleState()); };
        traduzir();
    }

    void traduzir() {
        mudo_.setTooltip(T("mudo"));
        solo_.setTooltip(T("solo"));
        fader_.setTitle(T("nível"));
    }

    // Os dados vêm do app; o que a mão está arrastando aqui não é atropelado.
    void setCanal(const Instrumento::Canal& c, juce::Colour corGrupo) {
        master_ = c.indice == -1;
        nome_ = c.nome;
        corGrupo_ = corGrupo;
        temNivel_ = c.tem("nivel");
        fader_.setVisible(temNivel_);
        mudo_.setVisible(c.tem("mudo"));
        solo_.setVisible(c.tem("solo"));
        fader_.corTampa = master_ ? cor::tinta : corGrupo;
        fader_.setFaixa(c.minDb, c.maxDb);
        if (!fader_.arrastando()) fader_.setDb(c.nivelDb);
        mudo_.setToggleState(c.mudo, juce::dontSendNotification);
        solo_.setToggleState(c.solo, juce::dontSendNotification);
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(master_ ? cor::pausa : cor::creme);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(master_ ? cor::masterBorda : cor::borda);
        g.drawRoundedRectangle(r, 8.0f, 1.0f);
        auto area = getLocalBounds().reduced(6, 8);
        g.setColour(master_ ? cor::tinta : corGrupo_);
        g.setFont(letra::forte(12.0f));
        g.drawFittedText(nome_, area.removeFromTop(30), juce::Justification::centred, 2, 0.7f);
        if (temNivel_) {
            g.setColour(cor::tinta);
            g.setFont(letra::mono(11.0f));
            g.drawText(textoDb(fader_.db()), dbArea_, juce::Justification::centred);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(6, 8);
        area.removeFromTop(30 + 4);
        const bool m = mudo_.isVisible(), s = solo_.isVisible();
        if (m || s) {
            auto ms = area.removeFromBottom(26);
            if (m && s) {
                mudo_.setBounds(ms.removeFromLeft(ms.getWidth() / 2 - 2));
                ms.removeFromLeft(4);
                solo_.setBounds(ms);
            } else {
                (m ? mudo_ : solo_).setBounds(ms);
            }
            area.removeFromBottom(4);
        }
        dbArea_ = area.removeFromBottom(16);
        area.removeFromBottom(2);
        fader_.setBounds(area.withSizeKeepingCentre(juce::jmin(40, area.getWidth()), area.getHeight()));
    }

private:
    Fader fader_;
    juce::TextButton mudo_, solo_;
    juce::String nome_;
    juce::Colour corGrupo_ { cor::teal };
    juce::Rectangle<int> dbArea_;
    bool master_ = false, temNivel_ = true;
};

// O bloco de um app: nome e as faixas do mixer dele (rola na horizontal).
class MixerDoApp final : public juce::Component {
public:
    static constexpr int kLarguraCanal = 88, kEspaco = 8;
    std::function<void(int indice, float db)> onNivel;
    std::function<void(int indice, bool)> onMudo, onSolo;

    MixerDoApp() {
        rolagem_.setViewedComponent(&fila_, false);
        rolagem_.setScrollBarsShown(false, true);
        rolagem_.setScrollBarThickness(8);
        addAndMakeVisible(rolagem_);
    }

    void traduzir() { for (auto& [i, c] : canais_) c->traduzir(); repaint(); }

    void setApp(const juce::String& nome, juce::Colour corGrupo, const Instrumento* inst, const juce::String& nota) {
        nome_ = nome; corGrupo_ = corGrupo; nota_ = nota;
        std::vector<int> vistos;
        if (inst != nullptr) {
            // canais do app primeiro, o master (−1) por último, à direita
            std::vector<const Instrumento::Canal*> ordem;
            for (const auto& c : inst->mixer) if (c.indice != -1) ordem.push_back(&c);
            for (const auto& c : inst->mixer) if (c.indice == -1) ordem.push_back(&c);
            for (const auto* c : ordem) {
                auto& comp = canais_[c->indice];
                if (comp == nullptr) {
                    comp = std::make_unique<CanalApp>();
                    fila_.addAndMakeVisible(*comp);
                    const int idx = c->indice;
                    comp->onNivel = [this, idx](float db) { if (onNivel) onNivel(idx, db); };
                    comp->onMudo = [this, idx](bool v) { if (onMudo) onMudo(idx, v); };
                    comp->onSolo = [this, idx](bool v) { if (onSolo) onSolo(idx, v); };
                }
                comp->setCanal(*c, corGrupo);
                vistos.push_back(c->indice);
            }
        }
        for (auto it = canais_.begin(); it != canais_.end();)
            it = std::find(vistos.begin(), vistos.end(), it->first) == vistos.end() ? canais_.erase(it) : std::next(it);
        ordem_ = vistos;
        posicionarFila();
        repaint();
    }
    int numCanais() const { return static_cast<int>(ordem_.size()); }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(cor::fundo);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(cor::borda);
        g.drawRoundedRectangle(r, 8.0f, 1.0f);
        g.setColour(corGrupo_);
        g.fillRoundedRectangle(14.0f, 14.0f, 6.0f, 20.0f, 3.0f);
        g.setColour(cor::tinta);
        g.setFont(letra::forte(15.0f));
        g.drawText(nome_.toUpperCase(), 28, 12, 400, 24, juce::Justification::centredLeft);
        if (nota_.isNotEmpty()) {
            g.setColour(cor::apagado);
            g.setFont(letra::sans(13.0f));
            g.drawFittedText(nota_, 28, 44, getWidth() - 48, 40, juce::Justification::topLeft, 2);
        }
    }

    void resized() override {
        rolagem_.setBounds(14, nota_.isNotEmpty() ? 96 : 46, getWidth() - 28, juce::jmax(0, getHeight() - 60));
        posicionarFila();
    }

private:
    // As faixas na altura da página; o master um pouco à parte, à direita.
    void posicionarFila() {
        const int h = juce::jmax(0, rolagem_.getHeight() - 12);
        int x = 0;
        for (const int idx : ordem_) {
            auto it = canais_.find(idx);
            if (it == canais_.end()) continue;
            if (idx == -1 && x > 0) x += 16;
            it->second->setBounds(x, 0, kLarguraCanal, h);
            x += kLarguraCanal + kEspaco;
        }
        fila_.setSize(juce::jmax(0, x - kEspaco), h);
    }
    std::vector<int> ordem_;
    juce::Viewport rolagem_;
    juce::Component fila_;
    std::map<int, std::unique_ptr<CanalApp>> canais_;
    juce::String nome_, nota_;
    juce::Colour corGrupo_ { cor::teal };
};

}  // namespace maestruc
