// Maestruc — peças da janela: fader, medidor, faixa (stem, master, fones),
// cartão de app, matriz de influências.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include "Idiomas.h"
#include "Visual.h"

#include <functional>

namespace maestruc {

inline juce::String u8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }
inline const juce::String& menos() { static const auto m = u8("\xe2\x88\x92"); return m; }   // −

inline juce::String textoDb(float db) {
    if (db <= -59.5f) return menos() + u8("\xe2\x88\x9e");   // −∞
    auto t = juce::String(std::abs(db), 1).replaceCharacter('.', ',');
    return (db < -0.05f ? menos() : juce::String()) + t;
}

inline float nivelDeRms(float rms) {   // 0…1 para desenhar, de -60 a 0 dBFS
    const float db = rms > 1.0e-6f ? 20.0f * std::log10(rms) : -120.0f;
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

// Fader vertical: -60 … +6 dB. Duplo clique volta a 0 dB. Arraste fino com Shift.
class Fader final : public juce::Component {
public:
    std::function<void(float)> onChange;
    juce::Colour corTampa = cor::teal;

    Fader() { setWantsKeyboardFocus(true); }
    // A faixa do fader (padrão −60…+6 dB); o mixer de um app usa a do
    // instrumento (16/11/2026).
    void setFaixa(float minimo, float maximo) {
        kMin = minimo; kMax = juce::jmax(maximo, minimo + 1.0f);
        db_ = juce::jlimit(kMin, kMax, db_);
        repaint();
    }
    bool arrastando() const { return arrastando_; }
    void setDb(float db, bool avisar = false) {
        db_ = juce::jlimit(kMin, kMax, db);
        repaint();
        if (avisar && onChange) onChange(db_);
    }
    float db() const { return db_; }

    void paint(juce::Graphics& g) override {
        const float trilhoX = getWidth() * 0.5f - 2.0f;
        g.setColour(cor::borda);
        g.fillRoundedRectangle(trilhoX, 0.0f, 4.0f, static_cast<float>(getHeight()), 2.0f);
        const float y0 = posDe(juce::jlimit(kMin, kMax, 0.0f));
        g.setColour(cor::bordaControle);
        g.drawHorizontalLine(static_cast<int>(y0 + 12.0f), 2.0f, static_cast<float>(getWidth() - 2));
        auto tampa = juce::Rectangle<float>(0.0f, posDe(db_), static_cast<float>(getWidth()), 24.0f).reduced(1.0f);
        g.setColour(cor::painel);
        g.fillRoundedRectangle(tampa, 4.0f);
        g.setColour(isEnabled() ? corTampa : cor::bordaControle);
        g.drawRoundedRectangle(tampa, 4.0f, hasKeyboardFocus(false) ? 3.0f : 2.0f);
        g.drawHorizontalLine(static_cast<int>(tampa.getCentreY()), tampa.getX() + 5.0f, tampa.getRight() - 5.0f);
    }
    void mouseDown(const juce::MouseEvent& e) override { inicioDb_ = db_; inicioY_ = e.position.y; arrastando_ = true; }
    void mouseUp(const juce::MouseEvent&) override { arrastando_ = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        const float escala = (kMax - kMin) / static_cast<float>(getHeight() - 24) * (e.mods.isShiftDown() ? 0.2f : 1.0f);
        setDb(inicioDb_ - (e.position.y - inicioY_) * escala, true);
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { setDb(0.0f, true); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { setDb(db_ + w.deltaY * 6.0f, true); }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k.getKeyCode() == juce::KeyPress::upKey) { setDb(db_ + 0.5f, true); return true; }
        if (k.getKeyCode() == juce::KeyPress::downKey) { setDb(db_ - 0.5f, true); return true; }
        return false;
    }

private:
    float kMin = -60.0f, kMax = 6.0f;
    bool arrastando_ = false;
    float posDe(float db) const { return (1.0f - (db - kMin) / (kMax - kMin)) * static_cast<float>(getHeight() - 24); }
    float db_ = 0.0f, inicioDb_ = 0.0f, inicioY_ = 0.0f;
};

class Medidor final : public juce::Component {
public:
    void setNivel(float n) { if (std::abs(n - nivel_) > 0.004f) { nivel_ = n; repaint(); } }
    bool mudo = false;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(cor::trilho);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(mudo ? cor::bordaControle : cor::teal);
        g.fillRoundedRectangle(r.removeFromBottom(r.getHeight() * nivel_), 3.0f);
    }

private:
    float nivel_ = 0.0f;
};

// Uma faixa do mixer: um stem (um app), o master ou os fones.
class Faixa final : public juce::Component {
public:
    enum Tipo { Stem, Master, Fones };
    std::function<void(float)> onGanho;
    std::function<void(bool)> onMudo, onSolo;
    std::function<void(int)> onRota;                         // stem: 0 mix, 1 fones, 2 nenhuma, 3 mix + fones
    std::function<void(const juce::String&)> onAparelho;     // master/fones: aparelho de saída ("" = nenhum)
    std::function<void()> onMixer;                           // stem: abrir o mixer próprio do app (16/11/2026)

    explicit Faixa(Tipo t) : tipo_(t) {
        for (auto* m : { &medL_, &medR_ }) addAndMakeVisible(*m);
        addAndMakeVisible(fader_);
        fader_.onChange = [this](float db) { if (onGanho) onGanho(db); repaint(); };
        if (tipo_ == Stem) {
            for (auto* b : { &mudo_, &solo_ }) {
                addAndMakeVisible(*b);
                b->setClickingTogglesState(true);
                estiloBotao(*b, cor::painel, cor::tinta, cor::bordaControle);
                b->getProperties().set("letra", 12.0f);
            }
            mudo_.setColour(juce::TextButton::buttonOnColourId, cor::pausa);
            solo_.setColour(juce::TextButton::buttonOnColourId, cor::tagMusica);
            mudo_.onClick = [this] { if (onMudo) onMudo(mudo_.getToggleState()); medL_.mudo = medR_.mudo = mudo_.getToggleState(); };
            solo_.onClick = [this] { if (onSolo) onSolo(solo_.getToggleState()); };
            addAndMakeVisible(mixer_);
            estiloBotao(mixer_, cor::painel, cor::tinta2, cor::bordaControle);
            mixer_.getProperties().set("letra", 11.0f);
            mixer_.onClick = [this] { if (onMixer) onMixer(); };
        }
        addAndMakeVisible(rota_);
        rota_.onChange = [this] {
            if (tipo_ == Stem) { if (onRota) onRota(rotaDeId(rota_.getSelectedId())); }
            else if (onAparelho) onAparelho(aparelhoAtual());
        };
        traduzir();
    }

    void traduzir() {
        mudo_.setTooltip(T("mudo"));
        solo_.setTooltip(T("solo"));
        mixer_.setButtonText("MIXER");
        mixer_.setTooltip(T("o mixer de dentro deste app, com os controles dele"));
        rota_.setTitle(T("saída"));
        fader_.setTitle(T("nível"));
        if (tipo_ == Stem) {
            const int sel = rota_.getSelectedId();
            rota_.clear(juce::dontSendNotification);
            rota_.addItem(T("mix"), 1);
            rota_.addItem(T("mix + fones"), 4);
            rota_.addItem(T("fones"), 2);
            rota_.addItem(T("nenhuma"), 3);
            rota_.setSelectedId(sel > 0 ? sel : 1, juce::dontSendNotification);
        } else {
            setAparelhos(aparelhos_, aparelhoAtual());
        }
        repaint();
    }

    // Master/fones: lista de aparelhos de saída e o escolhido.
    void setAparelhos(const juce::StringArray& lista, const juce::String& atual) {
        aparelhos_ = lista;
        rota_.clear(juce::dontSendNotification);
        rota_.addItem(T("nenhuma"), 1);
        for (int i = 0; i < lista.size(); ++i) rota_.addItem(lista[i], i + 2);
        const int idx = lista.indexOf(atual);
        rota_.setSelectedId(idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
    }
    juce::String aparelhoAtual() const {
        const int id = rota_.getSelectedId();
        return id >= 2 ? aparelhos_[id - 2] : juce::String();
    }

    void setTextos(const juce::String& grupo, const juce::String& nome, juce::Colour corGrupo, bool aceso) {
        grupo_ = grupo; nome_ = nome; corGrupo_ = corGrupo; aceso_ = aceso;
        fader_.corTampa = tipo_ == Stem ? corGrupo : cor::tinta;
        repaint();
    }
    // só o nome de baixo, ao trocar de língua (18/11/2026: "saída do app"
    // ficava em português nas faixas depois de mudar para EN, ES ou FR)
    void setNome(const juce::String& nome) { nome_ = nome; repaint(); }
    void setNiveis(float rms) {
        const float n = nivelDeRms(rms);
        medL_.setNivel(n);
        medR_.setNivel(n);
    }
    Fader& fader() { return fader_; }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        const bool master = tipo_ == Master;
        g.setColour(master ? cor::pausa : cor::creme);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(master ? cor::masterBorda : cor::borda);
        g.drawRoundedRectangle(r, 8.0f, 1.0f);
        auto area = getLocalBounds().reduced(8, 10);
        g.setColour(tipo_ == Stem ? corGrupo_ : cor::tinta);
        g.setFont(letra::sans(11.0f, true).withExtraKerningFactor(0.06f));
        g.drawFittedText(grupo_, area.removeFromTop(16), juce::Justification::centred, 1, 0.7f);
        area.removeFromTop(6);
        g.setColour(aceso_ ? cor::tinta : cor::apagado);
        g.setFont(letra::forte(13.0f));
        g.drawFittedText(nome_, area.removeFromTop(34), juce::Justification::centred, 2, 0.8f);
        g.setColour(cor::tinta);
        g.setFont(letra::mono(12.0f));
        g.drawText(textoDb(fader_.db()), dbArea_, juce::Justification::centred);
        g.setColour(cor::apagado);
        g.setFont(letra::sans(10.0f));
        g.drawText(T("saída"), rota_.getBounds().translated(0, -14).withHeight(12), juce::Justification::centredLeft);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8, 10);
        area.removeFromTop(16 + 6 + 34 + 6);
        rota_.setBounds(area.removeFromBottom(30));
        area.removeFromBottom(16 + 6);
        if (tipo_ == Stem) {
            auto ms = area.removeFromBottom(30);
            mudo_.setBounds(ms.removeFromLeft(ms.getWidth() / 2 - 2));
            ms.removeFromLeft(4);
            solo_.setBounds(ms);
            area.removeFromBottom(6);
            mixer_.setBounds(area.removeFromBottom(26));
            area.removeFromBottom(6);
        }
        dbArea_ = area.removeFromBottom(18);
        area.removeFromBottom(6);
        const int h = juce::jmin(220, area.getHeight());
        auto col = area.withSizeKeepingCentre(10 + 8 + 10 + 8 + 28, h);
        medL_.setBounds(col.removeFromLeft(10));
        col.removeFromLeft(8);
        medR_.setBounds(col.removeFromLeft(10));
        col.removeFromLeft(8);
        fader_.setBounds(col.removeFromLeft(28));
    }

private:
    static int rotaDeId(int id) { return id == 4 ? 3 : id - 1; }
    Tipo tipo_;
    juce::String grupo_, nome_;
    juce::Colour corGrupo_ = cor::tinta2;
    bool aceso_ = true;
    Medidor medL_, medR_;
    Fader fader_;
    juce::TextButton mudo_ { "M" }, solo_ { "S" }, mixer_ { "MIXER" };
    juce::ComboBox rota_;
    juce::StringArray aparelhos_;
    juce::Rectangle<int> dbArea_;
};

// Cartão de um app na coluna APPS.
class CartaoApp final : public juce::Component {
public:
    enum Tom { Ligado, Pausa, Desligado };
    std::function<void()> onAcao;
    std::function<void()> onVersaoNova;   // abre a página da release (18/11/2026)
    CartaoApp() {
        addAndMakeVisible(acao_);
        estiloBotao(acao_, cor::painel, cor::tinta, cor::bordaControle);
        acao_.getProperties().set("letra", 12.0f);
        acao_.onClick = [this] { if (onAcao) onAcao(); };
        addChildComponent(versaoNova_);
        estiloBotao(versaoNova_, cor::painel, cor::tealEscuro, cor::teal);
        versaoNova_.getProperties().set("letra", 11.0f);
        versaoNova_.onClick = [this] { if (onVersaoNova) onVersaoNova(); };
    }
    void setVersaoNova(const juce::String& texto) {
        versaoNova_.setButtonText(texto);
        versaoNova_.setVisible(texto.isNotEmpty());
    }
    void set(const juce::String& nome, const juce::String& estado, Tom tom, const juce::String& detalhe,
             const juce::String& acao, const juce::String& cpu, bool aberto) {
        nome_ = nome; estado_ = estado; tom_ = tom; detalhe_ = detalhe; cpu_ = cpu; aberto_ = aberto;
        acao_.setButtonText(acao);
        acao_.setVisible(acao.isNotEmpty());
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(cor::creme);
        g.fillRoundedRectangle(r, 8.0f);
        if (aberto_) {
            g.setColour(cor::teal);
            g.drawRoundedRectangle(r, 8.0f, 2.0f);
        } else {
            juce::Path p, tracejado;
            p.addRoundedRectangle(r, 8.0f);
            const float tr[] = { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(tracejado, p, tr, 2);
            g.setColour(cor::bordaControle);
            g.fillPath(tracejado);
        }
        auto a = getLocalBounds().reduced(12, 10);
        auto topo = a.removeFromTop(22);
        g.setFont(letra::sans(11.0f, true).withExtraKerningFactor(0.06f));
        const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), estado_) + 16.0f;
        auto pilula = juce::Rectangle<float>(topo.getRight() - w, topo.getY() + 2.0f, w, 18.0f);
        g.setColour(tom_ == Ligado ? cor::teal : tom_ == Pausa ? cor::pausa : cor::trilho);
        g.fillRoundedRectangle(pilula, 3.0f);
        g.setColour(tom_ == Ligado ? juce::Colours::white : tom_ == Pausa ? cor::pausaTexto : cor::tinta2);
        g.drawText(estado_, pilula, juce::Justification::centred);
        g.setColour(cor::tinta);
        g.setFont(letra::forte(16.0f));
        g.drawFittedText(nome_, topo.withTrimmedRight(static_cast<int>(w) + 6), juce::Justification::centredLeft, 1, 0.8f);
        a.removeFromTop(6);
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(12.0f));
        g.drawFittedText(detalhe_, a.removeFromTop(16), juce::Justification::centredLeft, 1, 0.75f);
        g.setFont(letra::mono(12.0f));
        g.drawText(cpu_, a.removeFromBottom(32), juce::Justification::centredRight);
    }
    void resized() override {
        auto a = getLocalBounds().reduced(12, 10);
        auto baixo = a.removeFromBottom(32);
        acao_.setBounds(baixo.removeFromLeft(110));
        baixo.removeFromLeft(6);
        versaoNova_.setBounds(baixo.removeFromLeft(juce::jmin(baixo.getWidth(), 120)));
    }

private:
    juce::String nome_, estado_, detalhe_, cpu_;
    Tom tom_ = Desligado;
    bool aberto_ = false;
    juce::TextButton acao_, versaoNova_;
};

// Matriz de influências: linha = quem influencia, coluna = quem é
// influenciado. A última coluna é o Master (o mix inteiro).
class Matriz final : public juce::Component {
public:
    static constexpr int kColunaMaster = 1000;
    std::function<void(int, int)> onEscolha;     // coluna = índice do stem ou kColunaMaster
    juce::StringArray nomes;
    std::function<float(int, int)> valor;        // 0 = nenhuma
    int linha = -1, coluna = -1;

    void paint(juce::Graphics& g) override {
        const int n = nomes.size();
        if (n < 1) {
            g.setColour(cor::apagado);
            g.setFont(letra::sans(13.0f));
            g.drawFittedText(T("Com um app ligado, ele já pode mudar o mix; com dois, um muda o outro."),
                             getLocalBounds().withTrimmedTop(8), juce::Justification::topLeft, 3);
            return;
        }
        g.setFont(letra::sans(11.0f, true));
        g.setColour(cor::tinta2);
        for (int c = 0; c <= n; ++c)
            g.drawFittedText(c < n ? nomes[c] : juce::String("Master"), celula(0, c).withY(0).withHeight(28), juce::Justification::centred, 2, 0.7f);
        for (int l = 0; l < n; ++l) {
            g.setColour(cor::tinta);
            g.setFont(letra::sans(12.0f, true));
            g.drawFittedText(nomes[l], juce::Rectangle<int>(0, celula(l, 0).getY(), kNome - 6, kAlt), juce::Justification::centredLeft, 1, 0.7f);
            for (int c = 0; c <= n; ++c) {
                const int dest = c < n ? c : kColunaMaster;
                auto r = celula(l, c).toFloat();
                const bool proprio = l == c;
                const float v = (proprio || !valor) ? 0.0f : valor(l, dest);
                const bool tem = std::abs(v) >= 1.0e-6f;
                g.setColour(proprio ? cor::trilho : !tem ? cor::creme : cor::ocre);
                g.fillRoundedRectangle(r, 4.0f);
                const bool sel = l == linha && dest == coluna;
                g.setColour(sel ? cor::tinta : c == n ? cor::masterBorda : cor::borda);
                g.drawRoundedRectangle(r.reduced(sel ? 1.5f : 0.5f), 4.0f, sel ? 3.0f : 1.0f);
                if (tem) {
                    g.setColour(juce::Colours::white);
                    g.setFont(letra::mono(12.0f, true));
                    g.drawText((v > 0 ? juce::String("+") : menos()) + juce::String(std::abs(v), 1).replaceCharacter('.', ','),
                               r, juce::Justification::centred);
                }
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int n = nomes.size();
        for (int l = 0; l < n; ++l)
            for (int c = 0; c <= n; ++c)
                if (l != c && celula(l, c).contains(e.getPosition())) {
                    linha = l;
                    coluna = c < n ? c : kColunaMaster;
                    repaint();
                    if (onEscolha) onEscolha(linha, coluna);
                    return;
                }
    }

private:
    static constexpr int kNome = 112, kLarg = 52, kAlt = 36, kGap = 4, kTopo = 32;   // 8 linhas + legenda cabem na área
    juce::Rectangle<int> celula(int l, int c) const { return { kNome + c * (kLarg + kGap), kTopo + l * (kAlt + kGap), kLarg, kAlt }; }
};

} // namespace maestruc
