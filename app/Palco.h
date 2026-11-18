// Maestruc — a janela: apps, saídas e stems, influências, gravação e regência.
// Desenhada em 1920 × 1080 (o layout aprovado) e escalada para caber.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Catalogo.h"
#include "Processos.h"
#include "Componentes.h"
#include "LiveHost.h"
#include "MixerApps.h"
#include "Orquestra.h"
#include "Textos.h"
#include "Versoes.h"

#include <map>

namespace maestruc {

inline juce::String virgula(double v, int casas) { return juce::String(v, casas).replaceCharacter('.', ','); }

struct AppVivo {
    EntradaCatalogo e;
    Processo proc;
    bool nosso = false;       // aberto pelo Maestruc
    bool pediuLigar = false;  // aberto fora, ligado por LIGAR
    bool ligado = false;      // som passando pelo Maestruc
    int stem = -1;
    double abertoEm = 0.0;
    int corIdx = 0;
};

struct Influencia {
    bool ativa = false;
    int alvo = 0;        // 0 nível (ganho), 1 filtro (brilho)
    int escuta = 0;      // 0 energia, 1 ataques, 2 densidade, 3 silêncio (Listen)
    float quantidade = -0.4f;
    float ms = 300.0f;
    int slot = -1;
};

// Uma linha da página REGÊNCIA: seed e peça de um app, ao abrir e com ele aberto.
class LinhaRegencia final : public juce::Component {
public:
    std::function<void()> onEnviarSeed, onEscolherPeca, onAbrirPeca, onTocar, onParar;
    juce::TextEditor seed;
    juce::String peca, nome, canal, resposta, abertura;

    LinhaRegencia() {
        addAndMakeVisible(seed);
        seed.setFont(letra::mono(14.0f));
        seed.setIndents(8, 7);
        for (auto* b : { &enviar_, &escolher_, &abrir_, &tocar_, &parar_ }) {
            addAndMakeVisible(*b);
            estiloBotao(*b, cor::painel, cor::tinta, cor::bordaControle);
            b->getProperties().set("letra", 12.0f);
        }
        tocar_.getProperties().set("icone", "play");
        parar_.getProperties().set("icone", "stop");
        enviar_.onClick = [this] { if (onEnviarSeed) onEnviarSeed(); };
        escolher_.onClick = [this] { if (onEscolherPeca) onEscolherPeca(); };
        abrir_.onClick = [this] { if (onAbrirPeca) onAbrirPeca(); };
        tocar_.onClick = [this] { if (onTocar) onTocar(); };
        parar_.onClick = [this] { if (onParar) onParar(); };
        traduzir();
    }
    void traduzir() {
        seed.setTextToShowWhenEmpty("seed", cor::apagado);
        enviar_.setButtonText(T("ENVIAR SEED"));
        escolher_.setButtonText(T("PEÇA…"));
        abrir_.setButtonText(T("ABRIR PEÇA"));
        tocar_.setButtonText(T("TOCAR"));
        parar_.setButtonText(T("PARAR"));
        repaint();
    }
    void setPode(bool seedVivo, bool abrirVivo, bool transporte) {
        enviar_.setEnabled(seedVivo);
        abrir_.setEnabled(abrirVivo && peca.isNotEmpty());
        tocar_.setEnabled(transporte);
        parar_.setEnabled(transporte);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(cor::trilho);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
        g.setColour(cor::tinta);
        g.setFont(letra::forte(16.0f));
        g.drawText(nome, 0, 8, 170, 22, juce::Justification::centredLeft);
        g.setFont(letra::sans(12.0f));
        g.setColour(cor::tinta2);
        g.drawFittedText(canal, 0, 32, 170, 40, juce::Justification::topLeft, 3);
        g.drawFittedText(abertura, 180, 50, 820, 18, juce::Justification::centredLeft, 1);
        g.setFont(letra::mono(12.0f));
        g.drawFittedText(peca.isEmpty() ? T("nenhuma peça escolhida") : juce::File(peca).getFileName(),
                         430, 10, 250, 34, juce::Justification::centredLeft, 2);
        g.setFont(letra::sans(12.0f));
        g.setColour(cor::tealEscuro);
        g.drawFittedText(resposta, 1110, 6, getWidth() - 1110, 64, juce::Justification::centredLeft, 4);
    }
    void resized() override {
        seed.setBounds(180, 10, 120, 34);
        enviar_.setBounds(306, 10, 116, 34);
        escolher_.setBounds(690, 10, 90, 34);
        abrir_.setBounds(786, 10, 110, 34);
        tocar_.setBounds(904, 10, 96, 34);
        parar_.setBounds(1006, 10, 96, 34);
    }

private:
    juce::TextButton enviar_, escolher_, abrir_, tocar_, parar_;
};

class Palco final : public juce::Component, private juce::Timer {
public:
    static constexpr int kW = 1920, kH = 1080;
    static constexpr int kCapacidade = 7;   // cabe na largura de SAÍDAS com fones e master

    Palco() {
        setLookAndFeel(&visual_);
        // Preferências pela convenção de pastas da família (18/11/2026):
        // <dados>/RASGO/Maestruc/Maestruc.settings. O arquivo de antes (o
        // padrão do JUCE: ~/RASGO/Maestruc.settings no Linux) é copiado uma vez.
        juce::PropertiesFile::Options antes;
        antes.applicationName = "Maestruc";
        antes.filenameSuffix = ".settings";
        antes.folderName = "RASGO";
        prefs_ = rasgo::folders::openSettings("Maestruc", antes.getDefaultFile());
        linguaAtual() = static_cast<Lingua>(juce::jlimit(0, 3, prefs_->getIntValue("lingua", 0)));

        // Motor e canal. No Linux com PipeWire/JACK, o host JACK (pega também
        // apps sem ponte de som); sem JACK (Windows, macOS) ou com
        // MAESTRUC_PORTATIL=1, o modo portátil: placa pelo JUCE, som dos apps
        // só pela ponte (15/11/2026).
        std::string err;
        const bool forcarPortatil = juce::SystemStats::getEnvironmentVariable("MAESTRUC_PORTATIL", {}) == "1";
        if (forcarPortatil || !host_.open("Maestruc", {}, err, kCapacidade)) {
            std::string e2;
            if (!abrirPortatil(e2))
                aviso_ = T("Sem áudio: ") + juce::String(err.empty() ? e2 : err + " / " + e2);
        } else {
            const auto aparelhos = host_.playbackDevices();
            const auto salvo = prefs_->getValue("saida_master");
            const auto escolhido = salvo.isNotEmpty() && std::find(aparelhos.begin(), aparelhos.end(), salvo.toStdString()) != aparelhos.end()
                                       ? salvo : (aparelhos.empty() ? juce::String() : juce::String(aparelhos.front()));
            std::string e2;
            if (escolhido.isNotEmpty()) host_.routeBus(0, escolhido.toStdString(), e2);
            saidaMaster_ = escolhido;
            const auto fones = prefs_->getValue("saida_fones");
            if (fones.isNotEmpty() && host_.routeBus(1, fones.toStdString(), e2)) saidaFones_ = fones;
        }
        juce::String erroOrq;
        if (!orq_.start(erroOrq)) aviso_ = T("Sem canal com os instrumentos: ") + erroOrq;
        orq_.onChange = [this] { atualizarCartoes(); atualizarRegencia(); atualizarMixers(); };

        // Cabeçalho.
        for (auto* b : { &tocar_, &parar_, &gravar_ }) {
            addAndMakeVisible(*b);
            b->getProperties().set("letra", 15.0f);
        }
        estiloBotao(tocar_, cor::teal, juce::Colours::white, cor::teal, 6.0f);
        tocar_.getProperties().set("icone", "play");
        estiloBotao(parar_, cor::painel, cor::tinta, cor::bordaControle, 6.0f);
        parar_.getProperties().set("icone", "stop");
        estiloBotao(gravar_, cor::painel, cor::vermelhoTexto, cor::vermelho, 6.0f);
        gravar_.getProperties().set("icone", "rec");
        gravar_.getProperties().set("corIcone", static_cast<juce::int64>(cor::vermelho.getARGB()));
        tocar_.onClick = [this] {
            orq_.enviarATodos("transport", "transport/play", juce::String("0"));
            if (contarCom("transport") == 0) avisar(T("Nenhum app ligado aceita tocar/parar do Maestruc."));
        };
        parar_.onClick = [this] { orq_.enviarATodos("transport", "transport/stop"); };
        gravar_.onClick = [this] { alternarGravacao(); };

        addAndMakeVisible(tempo_);
        tempo_.setEditable(true);
        tempo_.setFont(letra::mono(17.0f, true));
        tempo_.setText("120", juce::dontSendNotification);
        tempo_.setJustificationType(juce::Justification::centred);
        tempo_.onTextChange = [this] {
            const float bpm = tempo_.getText().replaceCharacter(',', '.').getFloatValue();
            if (bpm >= 20.0f && bpm <= 300.0f) orq_.enviarATodos("tempo", "tempo", bpm);
            tempo_.setText(juce::String(juce::roundToInt(juce::jlimit(20.0f, 300.0f, bpm > 0 ? bpm : 120.0f))), juce::dontSendNotification);
        };

        addChildComponent(voltarSaidas_);
        estiloBotao(voltarSaidas_, cor::painel, cor::tinta, cor::bordaControle);
        voltarSaidas_.onClick = [this] { pagina_ = 0; mostrarPagina(); };
        for (int i = 0; i < 5; ++i) {
            auto* b = abas_.add(new juce::TextButton());
            addAndMakeVisible(b);
            estiloBotao(*b, cor::painel, cor::tinta2, cor::painel, 0.0f);
            b->getProperties().remove("borda");
            b->getProperties().set("letra", 14.0f);
            b->onClick = [this, i] { pagina_ = i; mostrarPagina(); };
        }
        addAndMakeVisible(lingua_);
        lingua_.addItem("PT", 1);
        lingua_.addItem("EN", 2);
        lingua_.addItem("ES", 3);
        lingua_.addItem("FR", 4);
        lingua_.setSelectedId(static_cast<int>(linguaAtual()) + 1, juce::dontSendNotification);
        lingua_.onChange = [this] {
            linguaAtual() = static_cast<Lingua>(lingua_.getSelectedId() - 1);
            prefs_->setValue("lingua", lingua_.getSelectedId() - 1);
            traduzir();
        };

        // Apps. Instalados por padrão; as pastas de desenvolvimento com
        // MAESTRUC_FONTE=desenvolvimento (o ./run do código-fonte) ou a
        // preferência "fonte" (18/11/2026).
        {
            const auto env = juce::SystemStats::getEnvironmentVariable("MAESTRUC_FONTE", {});
            const auto escolha = env.isNotEmpty() ? env : prefs_->getValue("fonte", "instalados");
            fonte_ = escolha == "desenvolvimento" ? Fonte::Desenvolvimento : Fonte::Instalados;
        }
        juce::String avisoCatalogo;
        auto cat = carregarCatalogo(fonte_, avisoCatalogo);
        if (avisoCatalogo.isNotEmpty()) avisar(avisoCatalogo);
        // A lista de cartões rola quando não cabe acima do painel da máquina
        // (9 apps desde 15/11/2026: Trioio e Studio); os cartões não mudam.
        rolagemApps_.setViewedComponent(&listaApps_, false);
        rolagemApps_.setScrollBarsShown(true, false);
        rolagemApps_.setScrollBarThickness(8);
        addAndMakeVisible(rolagemApps_);
        for (int i = 0; i < cat.size(); ++i) {
            auto& a = apps_.emplace_back();
            a.e = cat[i];
            a.corIdx = i;
            auto* c = cartoes_.add(new CartaoApp());
            listaApps_.addAndMakeVisible(c);
            c->onAcao = [this, i] { acaoApp(i); };
            auto* l = regencia_.add(new LinhaRegencia());
            addChildComponent(l);
            l->nome = a.e.nome;
            l->seed.setText(prefs_->getValue("seed_" + a.e.nome), juce::dontSendNotification);
            l->peca = prefs_->getValue("peca_" + a.e.nome);
            l->seed.onTextChange = [this, i] { prefs_->setValue("seed_" + apps_[(size_t) i].e.nome, regencia_[i]->seed.getText().trim()); };
            l->onEnviarSeed = [this, i] { if (auto* inst = instDe(i)) orq_.enviar(inst->porta, "seed", regencia_[i]->seed.getText().trim()); };
            l->onEscolherPeca = [this, i] { escolherPeca(i); };
            l->onAbrirPeca = [this, i] { if (auto* inst = instDe(i)) orq_.enviar(inst->porta, "open", regencia_[i]->peca); };
            l->onTocar = [this, i] { if (auto* inst = instDe(i)) orq_.enviar(inst->porta, "transport/play", juce::String("0")); };
            auto* mx = mixers_.add(new MixerDoApp());
            addChildComponent(mx);
            auto* esc = escolhaMixer_.add(new juce::TextButton(a.e.nome.toUpperCase()));
            addChildComponent(esc);
            estiloBotao(*esc, cor::painel, cor::tinta, cor::bordaControle);
            esc->setColour(juce::TextButton::buttonOnColourId, cor::doGrupo(a.corIdx));
            esc->setColour(juce::TextButton::textColourOnId, cor::painel);   // legível em qualquer cor de grupo
            esc->setClickingTogglesState(false);
            esc->onClick = [this, i] { abrirMixerDe(i); };
            mx->onNivel = [this, i](int idx, float db) { if (auto* inst = instDe(i)) orq_.enviar(inst->porta, "mixer/nivel", idx, db); };
            mx->onMudo = [this, i](int idx, bool v) { if (auto* inst = instDe(i)) orq_.enviar(inst->porta, "mixer/mudo", idx, v ? 1 : 0); };
            mx->onSolo = [this, i](int idx, bool v) { if (auto* inst = instDe(i)) orq_.enviar(inst->porta, "mixer/solo", idx, v ? 1 : 0); };
            l->onParar = [this, i] { if (auto* inst = instDe(i)) orq_.enviar(inst->porta, "transport/stop"); };
        }
        for (int i = 0; i < 3; ++i) {
            auto* b = orcamento_.add(new juce::TextButton());
            addAndMakeVisible(b);
            estiloBotao(*b, cor::painel, cor::tinta, cor::bordaControle, 0.0f);
            b->setColour(juce::TextButton::buttonOnColourId, cor::teal);
            b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            b->getProperties().set("letra", 12.0f);
            b->setClickingTogglesState(true);
            b->setRadioGroupId(7);
            b->onClick = [this, i] { if (orcamento_[i]->getToggleState()) orq_.enviarATodos("budget", "budget", i); };
        }
        orcamento_[0]->setToggleState(true, juce::dontSendNotification);

        // Saídas: fones e master.
        fones_ = std::make_unique<Faixa>(Faixa::Fones);
        master_ = std::make_unique<Faixa>(Faixa::Master);
        for (auto* f : { fones_.get(), master_.get() }) addAndMakeVisible(*f);
        master_->onGanho = [this](float db) { host_.mixer().masterGainDb() = db; prefs_->setValue("ganho_master", db); };
        fones_->onGanho = [this](float db) { host_.mixer().fonesGainDb() = db; prefs_->setValue("ganho_fones", db); };
        master_->fader().setDb(static_cast<float>(prefs_->getDoubleValue("ganho_master", -3.0)), true);
        fones_->fader().setDb(static_cast<float>(prefs_->getDoubleValue("ganho_fones", -6.0)), true);
        master_->onAparelho = [this](const juce::String& d) { rotearBarramento(0, d); };
        fones_->onAparelho = [this](const juce::String& d) { rotearBarramento(1, d); };
        atualizarAparelhos();

        // Influências.
        addAndMakeVisible(matriz_);
        matriz_.valor = [this](int l, int c) {
            auto it = influencias_.find({ l, c });
            return it != influencias_.end() && it->second.ativa ? it->second.quantidade : 0.0f;
        };
        matriz_.onEscolha = [this](int, int) { mostrarInfluencia(); };
        addAndMakeVisible(alvo_);
        alvo_.onChange = [this] { aplicarInfluencia(); repaint(painelInf_); };
        addAndMakeVisible(escuta_);
        escuta_.onChange = [this] { aplicarInfluencia(); repaint(painelInf_); };
        for (auto* s : { &quantidade_, &suavidade_ }) {
            addAndMakeVisible(*s);
            s->setSliderStyle(juce::Slider::LinearHorizontal);
            s->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            s->onValueChange = [this] { aplicarInfluencia(); repaint(painelInf_); };
        }
        quantidade_.setRange(-1.0, 1.0, 0.01);
        quantidade_.setValue(-0.4, juce::dontSendNotification);
        quantidade_.setDoubleClickReturnValue(true, 0.0);
        suavidade_.setRange(10.0, 2000.0, 1.0);
        suavidade_.setSkewFactorFromMidPoint(300.0);
        suavidade_.setValue(300.0, juce::dontSendNotification);
        for (auto* b : { &desligarInf_, &novaInf_ }) {
            addAndMakeVisible(*b);
            b->getProperties().set("letra", 13.0f);
        }
        estiloBotao(desligarInf_, cor::painel, cor::tinta, cor::bordaControle);
        estiloBotao(novaInf_, cor::teal, juce::Colours::white, cor::teal);
        desligarInf_.onClick = [this] { ligarInfluencia(false); };
        novaInf_.onClick = [this] { ligarInfluencia(true); };

        // Gravação: Music/RASGO/Maestruc/recordings, pela convenção de pastas
        // da família (18/11/2026); o botão … escolhe outra, e a escolha fica
        // nas preferências. Cada take: <pasta>/YYYY-MM-DD/take-NN-HHMMSS.
        addAndMakeVisible(pasta_);
        pasta_.setFont(letra::mono(12.0f));
        pasta_.setIndents(10, 9);
        const auto padrao = rasgo::folders::material("Maestruc").getChildFile("recordings");
        pasta_.setText(prefs_->getValue("pasta_gravacao", padrao.getFullPathName()), juce::dontSendNotification);
        pasta_.onTextChange = [this] { prefs_->setValue("pasta_gravacao", pasta_.getText().trim()); };
        addAndMakeVisible(escolherPasta_);
        estiloBotao(escolherPasta_, cor::painel, cor::tinta, cor::bordaControle);
        escolherPasta_.onClick = [this] { escolherPastaGravacao(); };
        addAndMakeVisible(gravarMaster_);
        gravarMaster_.setToggleState(true, juce::dontSendNotification);
        addAndMakeVisible(gravarFones_);
        gravarFones_.setToggleState(false, juce::dontSendNotification);
        gravarFones_.setEnabled(false);   // os fones não são gravados (o retorno é de quem toca)

        for (auto* t : { &textoAprender_, &textoSobre_ }) {
            addChildComponent(*t);
            t->setMultiLine(true);
            t->setReadOnly(true);
            t->setCaretVisible(false);
            t->setFont(letra::sans(17.0f));
            t->setColour(juce::TextEditor::backgroundColourId, cor::painel);
            t->setColour(juce::TextEditor::outlineColourId, cor::painel);
            t->setIndents(28, 24);
        }

        // Versão nova (18/11/2026): o guardado vale já; a pergunta da primeira
        // abertura e a rodada do dia vêm depois de a janela aparecer.
        {
            juce::int64 quando = 0;
            publicadas_ = versoes::lerGuardado(prefs_->getValue("versoes_guardadas"), quando);
        }
        for (int i = 0; i < cartoes_.size(); ++i)
            cartoes_[i]->onVersaoNova = [this, i] {
                const auto it = publicadas_.find(apps_[(size_t) i].e.repositorio);
                if (it != publicadas_.end()) juce::URL(it->second.pagina).launchInDefaultBrowser();
            };
        addChildComponent(verificarVersoes_);
        verificarVersoes_.setToggleState(prefs_->getValue("verificar_versoes") == "sim", juce::dontSendNotification);
        verificarVersoes_.onClick = [this] {
            prefs_->setValue("verificar_versoes", verificarVersoes_.getToggleState() ? "sim" : "nao");
            if (verificarVersoes_.getToggleState()) verificarVersoesSePreciso();
            repaint(areaPagina_);
        };
        if (fonte_ == Fonte::Instalados) {
            juce::Component::SafePointer<Palco> seguro(this);
            juce::Timer::callAfterDelay(1500, [seguro] { if (seguro != nullptr) seguro->perguntarOuVerificar(); });
        }

        inicio_ = juce::Time::getMillisecondCounterHiRes();
        setSize(kW, kH);   // por último: resized() posiciona o que já existe
        traduzir();
        atualizarApps();
        // MAESTRUC_PAGINA=0…4 abre numa aba (capturas de tela e documentação,
        // sem clique; 16/11/2026). Sem ela, SAÍDAS como sempre.
        pagina_ = juce::jlimit(0, 4, juce::SystemStats::getEnvironmentVariable("MAESTRUC_PAGINA", "0").getIntValue());
        mostrarPagina();
        startTimerHz(30);
    }

    ~Palco() override {
        stopTimer();
        if (cbPlaca_) placa_.removeAudioCallback(cbPlaca_.get());   // o áudio para antes do host fechar
        host_.stopRecording();
        prefs_->saveIfNeeded();
        orq_.stop();
        host_.close();   // devolve às caixas os apps tomados; os apps continuam abertos
        setLookAndFeel(nullptr);
    }

    // A logo do Maestruc (assets/identidade, variação R4 do autor) desenhada em
    // vetor: as mesmas curvas do SVG, sem o quadrado de fundo nem o contorno.
    static void desenharLogo(juce::Graphics& g, juce::Rectangle<float> area) {
        // caixa das curvas no SVG 1024: x 230..818, y 232..792
        const float x0 = 230.0f, y0 = 232.0f, w = 588.0f, h = 560.0f;
        const float k = juce::jmin(area.getWidth() / w, area.getHeight() / h);
        const float ox = area.getX() + (area.getWidth() - w * k) * 0.5f;
        const float oy = area.getY() + (area.getHeight() - h * k) * 0.5f;
        auto P = [&](float x, float y) { return juce::Point<float>(ox + (x - x0) * k, oy + (y - y0) * k); };
        const float opac[] = { 0.61f, 0.59f, 0.58f, 0.56f, 0.56f, 0.58f, 0.59f, 0.61f };
        const float traco = juce::jmax(1.4f, 22.0f * k);
        for (int i = 0; i < 8; ++i) {
            const float y = 232.0f + 80.0f * static_cast<float>(i);
            juce::Path p;
            p.startNewSubPath(P(230.0f, y));
            p.cubicTo(P(480.0f, y), P(560.0f, 512.0f), P(760.0f, 512.0f));
            g.setColour(cor::teal.withAlpha(opac[i]));
            g.strokePath(p, juce::PathStrokeType(traco, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto c = P(760.0f, 512.0f);
        const float r = 58.0f * k;
        g.setColour(cor::vermelho);
        g.fillEllipse(c.x - r, c.y - r, 2.0f * r, 2.0f * r);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(cor::fundo);
        // Cabeçalho.
        g.setColour(cor::painel);
        g.fillRect(0, 0, kW, 72);
        g.setColour(cor::borda);
        g.drawHorizontalLine(71, 0.0f, static_cast<float>(kW));
        // Logo ao lado do título (autor, 18/11/2026): a R4 sem contorno nem
        // fundo, as oito linhas que se juntam no Master.
        desenharLogo(g, { 24.0f, 12.0f, 48.0f, 48.0f });   // como o Navalha: à esquerda, quase a altura do cabeçalho
        g.setColour(cor::tinta);
        g.setFont(letra::forte(26.0f).withExtraKerningFactor(0.06f));
        g.drawText("MAESTRUC", 84, 18, 120, 36, juce::Justification::centredLeft);
        g.setColour(cor::apagado);
        g.setFont(letra::sans(13.0f));
        g.drawFittedText(T("família RASGO tocando junto"), 212, 26, 184, 24, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour(cor::borda);
        g.drawVerticalLine(404, 16.0f, 56.0f);
        g.setColour(cor::apagado);
        g.setFont(letra::sans(11.0f).withExtraKerningFactor(0.08f));
        g.drawText(T("SESSÃO"), 420, 16, 120, 16, juce::Justification::centredLeft);
        g.setColour(cor::tinta);
        g.setFont(letra::sans(15.0f, true));
        g.drawText(T("Ensaio") + " " + juce::Time::getCurrentTime().formatted("%d/%m"), 420, 32, 130, 22, juce::Justification::centredLeft);
        g.setColour(cor::borda);
        g.drawVerticalLine(1040, 16.0f, 56.0f);
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(13.0f));
        g.drawFittedText(T("tempo comum"), 1052, 24, 96, 24, juce::Justification::centredRight, 1, 0.8f);
        g.setColour(cor::tinta);
        g.setFont(letra::mono(15.0f));
        g.drawText(relogio((juce::Time::getMillisecondCounterHiRes() - inicio_) / 1000.0), 1216, 24, 70, 24, juce::Justification::centredLeft);
        if (aviso_.isNotEmpty()) {
            g.setColour(cor::vermelhoTexto);
            g.setFont(letra::sans(12.0f));
            g.drawFittedText(aviso_, 1296, 8, abas_[0]->getX() - 1310, 56, juce::Justification::centredLeft, 3);
        }

        auto painel = [&](juce::Rectangle<int> r) {
            g.setColour(cor::painel);
            g.fillRoundedRectangle(r.toFloat(), 10.0f);
            g.setColour(cor::borda);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10.0f, 1.0f);
        };
        auto titulo = [&](int x, int y, const juce::String& t, const juce::String& sub) {
            g.setColour(cor::tinta2);
            g.setFont(letra::sans(13.0f, true).withExtraKerningFactor(0.1f));
            g.drawText(t, x, y, 400, 18, juce::Justification::centredLeft);
            const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), t);
            g.setColour(cor::apagado);
            g.setFont(letra::sans(12.0f));
            g.drawFittedText(sub, x + static_cast<int>(w) + 14, y, 1000 - static_cast<int>(w), 18, juce::Justification::centredLeft, 1, 0.8f);
        };
        painel(areaApps_);
        titulo(areaApps_.getX() + 18, areaApps_.getY() + 18, "APPS", {});
        desenharMaquina(g);
        if (pagina_ == 0) {
            painel(areaSaidas_);
            titulo(areaSaidas_.getX() + 20, areaSaidas_.getY() + 16, T("SAÍDAS E STEMS"),
                   T("o som de cada app chega aqui separado; nível, mudo, solo e para onde vai"));
            if (host_.stems().empty()) {
                g.setColour(cor::apagado);
                g.setFont(letra::sans(14.0f));
                g.drawFittedText(T("Abra um app na coluna APPS: o som dele aparece aqui."),
                                 areaSaidas_.reduced(24).withTrimmedTop(40).withWidth(600), juce::Justification::topLeft, 2);
            }
            painel(areaInf_);
            titulo(areaInf_.getX() + 20, areaInf_.getY() + 16, T("INFLUÊNCIAS"),
                   T("o que um stem faz muda outro · linha = quem influencia · coluna = quem é influenciado"));
            desenharDetalheInfluencia(g);
            painel(areaGrav_);
            desenharGravacao(g);
        } else {
            painel(areaPagina_);
            if (pagina_ == 1) {
                titulo(areaPagina_.getX() + 20, areaPagina_.getY() + 16, T("MIXERS"),
                       T("o mixer de dentro de cada app, com os mesmos controles dele; o que muda no app volta para cá"));
                if (mixersVisiveis_ == 0) {
                    g.setColour(cor::apagado);
                    g.setFont(letra::sans(14.0f));
                    g.drawFittedText(T("Abra um app na coluna APPS: o mixer dele aparece aqui."),
                                     areaPagina_.reduced(24).withTrimmedTop(40).withWidth(700), juce::Justification::topLeft, 2);
                }
            }
            if (pagina_ == 2)
                titulo(areaPagina_.getX() + 20, areaPagina_.getY() + 16, T("REGÊNCIA"),
                       T("seed e peça de cada app: ao abrir pelo Maestruc, ou enviadas com ele aberto"));
            if (pagina_ == 4) {
                const int y0 = areaPagina_.getBottom() - kAlturaVersoes;
                titulo(areaPagina_.getX() + 20, y0 + 14, T("VERSÕES DOS INSTRUMENTOS"),
                       T("só avisa: o link leva à página oficial; o Maestruc nunca baixa nem instala"));
                desenharVersoes(g, juce::Rectangle<int>(areaPagina_.getX() + 20, y0 + 80, areaPagina_.getWidth() - 40,
                                                        kAlturaVersoes - 90));
            }
        }
        if (auto* b = abas_[pagina_]) {
            g.setColour(cor::teal);
            g.fillRect(b->getX(), b->getBottom() - 3, b->getWidth(), 3);
        }
        // Rodapé (18/11/2026), no padrão da família: autoria, nome e ano,
        // licença e versão, na margem abaixo dos painéis.
        {
           #ifdef JUCE_APPLICATION_VERSION_STRING
            const juce::String versao = JUCE_APPLICATION_VERSION_STRING;
           #else
            const juce::String versao;
           #endif
            const auto sep = u8("  \xc2\xb7  ");
            g.setColour(cor::apagado);
            g.setFont(letra::sans(10.5f, true).withExtraKerningFactor(0.08f));
            g.drawText(u8("\xc2\xa9 L\xc3\x9a" "CIO DE ARA\xc3\x9a" "JO") + sep + "MAESTRUC 2026" + sep + T("LICENÇA AGPLv3+")
                           + sep + "v" + versao,
                       0, 1058, kW, 20, juce::Justification::centred);
        }
    }

    void resized() override {
        posicionarCabecalho();
        areaApps_ = { 24, 96, 340, 960 };
        areaSaidas_ = { 388, 96, 1080, 500 };
        areaInf_ = { 388, 620, 1080, 436 };
        areaGrav_ = { 1492, 96, 404, 960 };
        areaPagina_ = { 388, 96, 1508, 960 };
        int y = areaApps_.getY() + 46;
        {
            const int espaco = (areaApps_.getBottom() - 72 - 112 - 10) - y;   // 7 cartões de 96 + 6 cabem
            rolagemApps_.setBounds(areaApps_.getX() + 18, y, 304 + 12, espaco);
            listaApps_.setSize(304, juce::jmax(0, cartoes_.size() * 102 - 6));
            int cy = 0;
            for (auto* c : cartoes_) { c->setBounds(0, cy, 304, 96); cy += 102; }
        }
        auto orc = juce::Rectangle<int>(areaApps_.getX() + 18, areaApps_.getBottom() - 72, 304, 36);
        for (auto* b : orcamento_) b->setBounds(orc.removeFromLeft(304 / 3));
        posicionarFaixas();
        // Influências: matriz (até 7 apps + Master) e o detalhe ao lado.
        matriz_.setBounds(areaInf_.getX() + 20, areaInf_.getY() + 44, 112 + 8 * 56, 7 * 44 + 32);
        painelInf_ = { matriz_.getRight() + 20, areaInf_.getY() + 44, 0, 340 };
        painelInf_.setRight(areaInf_.getRight() - 20);
        auto p = painelInf_.reduced(16, 14);
        p.removeFromTop(18 + 10 + 24 + 10);
        auto linha = p.removeFromTop(56);
        escuta_.setBounds(linha.removeFromLeft(linha.getWidth() / 2 - 5).withTrimmedTop(20));
        linha.removeFromLeft(10);
        alvo_.setBounds(linha.withTrimmedTop(20));
        p.removeFromTop(10);
        quantidade_.setBounds(p.removeFromTop(30).withTrimmedLeft(96).withTrimmedRight(70));
        p.removeFromTop(8);
        suavidade_.setBounds(p.removeFromTop(30).withTrimmedLeft(96).withTrimmedRight(70));
        auto bt = p.removeFromBottom(36);
        desligarInf_.setBounds(bt.removeFromLeft(130));
        bt.removeFromLeft(8);
        novaInf_.setBounds(bt.removeFromLeft(220));
        // Gravação.
        pasta_.setBounds(areaGrav_.getX() + 20, areaGrav_.getY() + 66, 404 - 40 - 44, 36);
        escolherPasta_.setBounds(pasta_.getRight() + 6, pasta_.getY(), 38, 36);
        posicionarPistas();
        int ry = areaPagina_.getY() + 50;
        for (auto* l : regencia_) { l->setBounds(areaPagina_.getX() + 20, ry, areaPagina_.getWidth() - 40, 78); ry += 82; }
        posicionarMixers();
        textoAprender_.setBounds(areaPagina_.reduced(2));
        textoSobre_.setBounds(areaPagina_.reduced(2).withTrimmedBottom(kAlturaVersoes));
        verificarVersoes_.setBounds(areaPagina_.getX() + 20, areaPagina_.getBottom() - kAlturaVersoes + 44,
                                    areaPagina_.getWidth() - 40, 26);
    }

private:
    // ——— Língua ———
    void traduzir() {
        tocar_.setButtonText(T("TOCAR TODOS"));
        parar_.setButtonText(T("PARAR"));
        gravar_.setButtonText(host_.recording() ? T("PARAR REC") : T("GRAVAR"));
        tempo_.setTooltip(T("tempo comum: digite e Enter para mandar a todos que aceitam"));
        const char* abas[] = { "SAÍDAS", "MIXERS", "REGÊNCIA", "APRENDER", "SOBRE" };
        for (int i = 0; i < 5; ++i) abas_[i]->setButtonText(T(abas[i]));
        for (auto* m : mixers_) m->traduzir();
        voltarSaidas_.setButtonText(T("VOLTAR A SAÍDAS"));
        verificarVersoes_.setButtonText(T("verificar versões novas pela internet, uma vez por dia"));
        atualizarMixers();
        const char* orc[] = { "NORMAL", "ECONÔMICO", "MÍNIMO" };
        for (int i = 0; i < 3; ++i) orcamento_[i]->setButtonText(T(orc[i]));
        lingua_.setTitle(T("Língua"));
        desligarInf_.setButtonText(T("DESLIGAR"));
        novaInf_.setButtonText(T("+ NOVA INFLUÊNCIA"));
        escolherPasta_.setButtonText(u8("\xe2\x80\xa6"));
        escolherPasta_.setTooltip(T("escolher a pasta das gravações"));
        gravarMaster_.setTitle(T("gravar o Master"));
        const int a = alvo_.getSelectedId();
        const int e = escuta_.getSelectedId();
        alvo_.clear(juce::dontSendNotification);
        alvo_.addItem(T("nível (no Maestruc)"), 1);
        alvo_.addItem(T("filtro (no Maestruc)"), 2);
        alvo_.addItem(T("densidade (pedido ao app) — ainda não"), 3);
        alvo_.setItemEnabled(3, false);
        alvo_.setSelectedId(a > 0 ? a : 1, juce::dontSendNotification);
        escuta_.clear(juce::dontSendNotification);
        escuta_.addItem(T("energia"), 1);
        escuta_.addItem(T("ataques"), 2);
        escuta_.addItem(T("densidade"), 3);
        escuta_.addItem(T("silêncio"), 4);
        escuta_.setSelectedId(e > 0 ? e : 1, juce::dontSendNotification);
        for (size_t i = 0; i < apps_.size(); ++i) {
            auto* l = regencia_[static_cast<int>(i)];
            const auto& ab = apps_[i].e.abertura;
            l->abertura = ab == "batruc" ? T("ao abrir: usa a peça (.batruc.txt) e a seed (8 hexadecimais) daqui")
                        : ab == "modular" || ab == "seed" ? T("ao abrir: usa a seed daqui (número decimal)")
                        : ab == "navalha" ? T("ao abrir: usa o projeto daqui (.navalha ou .zip)")
                                          : T("ao abrir: ainda não aceita peça nem seed");
            l->traduzir();
        }
        for (auto* f : faixas_) { f->traduzir(); f->setNome(T("saída do app")); }
        fones_->traduzir();
        master_->traduzir();
        fones_->setTextos(T("FONES"), T("Retorno"), cor::tinta, true);
        master_->setTextos("MIX", "Master", cor::tinta, true);
        textoAprender_.setText(textoAprender(linguaAtual()));
        textoSobre_.setText(textoSobre(linguaAtual()));
        posicionarCabecalho();
        atualizarCartoes();
        atualizarRegencia();
        repaint();
    }

    void posicionarCabecalho() {
        const auto larg = [](juce::TextButton& b, float letraTam) {
            return juce::roundToInt(juce::GlyphArrangement::getStringWidth(letra::sans(letraTam, true), b.getButtonText()));
        };
        int x = 560;
        tocar_.setBounds(x, 14, larg(tocar_, 15.0f) + 64, 44);
        x = tocar_.getRight() + 10;
        parar_.setBounds(x, 14, larg(parar_, 15.0f) + 60, 44);
        x = parar_.getRight() + 10;
        gravar_.setBounds(x, 14, juce::jmax(140, larg(gravar_, 15.0f) + 60), 44);
        tempo_.setBounds(1150, 22, 56, 28);
        lingua_.setBounds(kW - 24 - 68, 18, 68, 36);
        int largura = 0;
        juce::Array<int> ws;
        for (auto* b : abas_) { ws.add(larg(*b, 14.0f) + 24); largura += ws.getLast() + 2; }
        int ax = lingua_.getX() - 16 - largura;
        for (int i = 0; i < abas_.size(); ++i) { abas_[i]->setBounds(ax, 16, ws[i], 44); ax += ws[i] + 2; }
    }

    // ——— Apps ———
    const Instrumento* instDe(int i) const { return orq_.porNome(apps_[static_cast<size_t>(i)].e.nome); }

    void acaoApp(int i) {
        auto& a = apps_[static_cast<size_t>(i)];
        const bool aberto = processoVivo(a.proc.pid);
        if (!aberto && !a.e.achado()) {
            // só avisar como instalar (decisão do autor, 15/11): a página oficial
            if (a.e.pagina.startsWith("https://")) juce::URL(a.e.pagina).launchInDefaultBrowser();
            return;
        }
        if (!aberto) {
            juce::StringArray extras;
            const auto seed = regencia_[i]->seed.getText().trim().toUpperCase();
            const auto& peca = regencia_[i]->peca;
            if (a.e.abertura == "batruc") {
                if (peca.isNotEmpty() && juce::File(peca).existsAsFile()) extras.addArray({ "--abrir", peca });
                if (seed.length() == 8 && seed.containsOnly("0123456789ABCDEF")) extras.addArray({ "--seed", seed });
            } else if (a.e.abertura == "modular" && seed.isNotEmpty() && seed.containsOnly("0123456789")) {
                extras.add(seed);
            } else if (a.e.abertura == "seed" && seed.isNotEmpty() && seed.containsOnly("0123456789")) {
                extras.addArray({ "--seed", seed });
            } else if (a.e.abertura == "navalha" && peca.isNotEmpty() && juce::File(peca).existsAsFile()) {
                extras.addArray({ "--abrir", peca });
            }
            juce::String erro;
            a.proc = {};
            a.proc.lancador = abrirApp(a.e, extras, erro);
            a.proc.pid = static_cast<int>(a.proc.lancador);
            a.nosso = a.proc.lancador > 0;
            a.ligado = false;
            a.abertoEm = agora();
            if (!a.nosso) avisar(a.e.nome + ": " + erro);
        } else if (!a.ligado && !a.nosso) {
            a.pediuLigar = true;   // aberto fora: ligar ao Maestruc
        } else {
            fecharApp(a.proc);
        }
        atualizarApps();
    }

    void atualizarApps() {
        bool mudouStems = false;
        for (auto& a : apps_) {
            if (a.proc.lancador > 0 && lancadorSaiu(a.proc.lancador)) {
                a.proc.lancador = 0;
                if (!processoVivo(a.proc.pid)) a.proc.pid = 0;
            }
            if (!processoVivo(a.proc.pid)) {
                const int achado = acharPid(a.e.processo);
                if (achado != a.proc.pid) { a.proc.pid = achado; a.proc.medidoEm = 0.0; }
            }
            if (a.proc.pid <= 0) {
                if (agora() - a.abertoEm > 20.0) a.nosso = false;
                if (host_.hasBridge(slug(a.e.nome))) host_.detachBridge(slug(a.e.nome));
                a.ligado = false;
                a.pediuLigar = false;
                continue;
            }
            medirCpu(a.proc);
            // Ponte de som (15/11/2026): o app anunciou o caminho; ela tem
            // preferência sobre a porta JACK e é o único caminho no modo
            // portátil. Por ora só a mistura vira stem (os canais internos
            // esperam o desenho das subfaixas).
            if (const auto* inst = orq_.porNome(a.e.nome);
                inst && inst->ponte.isNotEmpty() && (a.nosso || a.pediuLigar || a.ligado) && host_.running()
                && !host_.hasBridge(slug(a.e.nome))) {
                std::string err;
                const auto stems = host_.attachBridge(slug(a.e.nome), inst->ponte.toStdString(), err, 1);
                if (!stems.empty()) {
                    if (stems[0] != a.stem) mudouStems = true;
                    a.stem = stems[0];
                    a.ligado = true;
                } else {
                    avisar(a.e.nome + ": " + juce::String(err));
                }
            }
            if (!a.ligado && (a.nosso || a.pediuLigar) && host_.running() && !host_.portable()
                && !host_.outputPorts(a.e.padraoSom.toStdString()).empty()) {
                std::string err;
                const int s = host_.addStem(slug(a.e.nome), err);
                if (s >= 0) {
                    if (s != a.stem) mudouStems = true;
                    a.stem = s;
                    err.clear();
                    a.ligado = !host_.connectSource(slug(a.e.nome), a.e.padraoSom.toStdString(), err, true).empty();
                    if (!a.ligado && !err.empty()) avisar(a.e.nome + ": " + juce::String(err));
                } else {
                    avisar(a.e.nome + ": " + juce::String(err));
                }
            }
        }
        if (host_.running() && !host_.portable()) host_.enforceTaken();
        if (++verificacoes_ % 10 == 0) atualizarAparelhos();   // ~5 s: aparelho ligado ou desligado
        if (mudouStems || faixas_.size() != static_cast<int>(host_.stems().size())) reconstruirStems();
        atualizarCartoes();
        atualizarRegencia();
    }

    void atualizarCartoes() {
        for (size_t i = 0; i < apps_.size(); ++i) {
            auto& a = apps_[i];
            const bool aberto = a.proc.pid > 0;
            const auto* inst = orq_.porNome(a.e.nome);
            juce::String estado, acao, detalhe = T(a.e.nota);
            auto tom = CartaoApp::Desligado;
            if (!aberto && !a.e.achado()) {
                const bool dev = fonte_ == Fonte::Desenvolvimento;
                estado = dev ? T("SEM BUILD") : T("NÃO INSTALADO");
                detalhe = dev ? T("não achei o build nesta pasta de desenvolvimento") : T("instale pela página oficial");
                acao = !dev && a.e.pagina.isNotEmpty() ? T("COMO INSTALAR") : juce::String();
            } else if (!aberto) {
                const bool abrindo = a.nosso && agora() - a.abertoEm < 20.0;
                estado = abrindo ? T("ABRINDO") : T("FECHADO");
                tom = abrindo ? CartaoApp::Pausa : CartaoApp::Desligado;
                acao = abrindo ? juce::String() : T("ABRIR");
            } else if (a.ligado) {
                const bool tocando = inst && inst->tocando;
                estado = inst ? (tocando ? T("TOCANDO") : T("PARADO")) : T("LIGADO");
                tom = tocando || !inst ? CartaoApp::Ligado : CartaoApp::Pausa;
                acao = T("FECHAR");
            } else if (a.nosso || a.pediuLigar) {
                estado = T("ABRINDO");
                tom = CartaoApp::Pausa;
                detalhe = T("esperando o som do app aparecer…");
                // O script de alguns apps compila antes de abrir (Antitotem):
                // mostrar o progresso em vez de parecer parado.
                if (a.proc.pid == a.proc.lancador) {
                    const auto pct = progressoCompilacao(a.e);
                    if (pct.isNotEmpty()) {
                        estado = T("COMPILANDO");
                        detalhe = T("compilando") + " " + pct + T(" — a primeira vez leva alguns minutos");
                    }
                }
            } else {
                estado = T("FORA");
                tom = CartaoApp::Pausa;
                acao = T("LIGAR");
                detalhe = T("aberto fora do Maestruc");
            }
            const auto cpu = aberto ? "CPU " + virgula(a.proc.cpu, 2) : juce::String();
            // versão nova (18/11/2026): só com o app fechado e instalado
            juce::String botaoVersao;
            if (!aberto && a.e.achado()) {
                const auto it = publicadas_.find(a.e.repositorio);
                if (it != publicadas_.end() && versoes::haVersaoNova(a.e.versaoInstalada, it->second.versao)) {
                    detalhe = "v" + versoes::limpar(a.e.versaoInstalada) + " " + T("instalada") + u8(" \xc2\xb7 v") + it->second.versao
                              + " " + T("disponível");
                    botaoVersao = T("VERSÃO NOVA");
                }
            }
            cartoes_[static_cast<int>(i)]->setVersaoNova(botaoVersao);
            cartoes_[static_cast<int>(i)]->set(a.e.nome, estado, tom, detalhe, acao, cpu, aberto && a.ligado);
        }
        repaint(areaApps_);
    }

    void atualizarRegencia() {
        for (size_t i = 0; i < apps_.size(); ++i) {
            auto* l = regencia_[static_cast<int>(i)];
            const auto* inst = orq_.porNome(apps_[i].e.nome);
            l->canal = inst ? T("canal aberto: ") + inst->caps.joinIntoString(", ") : T("sem canal com o Maestruc");
            l->resposta = {};
            if (inst && inst->seed.isNotEmpty()) l->resposta = T("seed atual") + " " + inst->seed;
            if (inst && inst->ultimaResposta.isNotEmpty())
                l->resposta << (l->resposta.isEmpty() ? "" : "\n") << T("última resposta") << ": " << inst->ultimaResposta;
            l->setPode(inst && inst->tem("seed"), inst && inst->tem("open"), inst && inst->tem("transport"));
        }
    }

    // ——— Mixers dos apps (16/11/2026) ———
    // O mixer próprio de um instrumento (botão MIXER na faixa dele, ou a
    // fileira no alto da página MIXERS).
    void abrirMixerDe(int i) {
        mixerEscolhido_ = i;
        pagina_ = 1;
        mostrarPagina();
        atualizarMixers();
    }
    void atualizarMixers() {
        mixersVisiveis_ = 0;
        // se o escolhido fechou, o primeiro aberto
        if (mixerEscolhido_ < 0 || orq_.porNome(apps_[(size_t) mixerEscolhido_].e.nome) == nullptr) {
            mixerEscolhido_ = -1;
            for (size_t i = 0; i < apps_.size(); ++i)
                if (orq_.porNome(apps_[i].e.nome) != nullptr) { mixerEscolhido_ = static_cast<int>(i); break; }
        }
        for (size_t i = 0; i < apps_.size(); ++i) {
            auto* m = mixers_[static_cast<int>(i)];
            auto* esc = escolhaMixer_[static_cast<int>(i)];
            const auto* inst = orq_.porNome(apps_[i].e.nome);
            const bool escolhido = static_cast<int>(i) == mixerEscolhido_;
            esc->setVisible(pagina_ == 1 && inst != nullptr);
            esc->setToggleState(escolhido, juce::dontSendNotification);
            m->setVisible(pagina_ == 1 && inst != nullptr && escolhido);
            if (inst == nullptr) continue;
            ++mixersVisiveis_;
            const bool temMixer = inst->tem("mixer") && !inst->mixer.empty();
            const juce::String nota = temMixer ? juce::String()
                : inst->tem("mixer") ? T("esperando a lista do mixer do app")
                : T("este app não oferece o mixer dele ao Maestruc");
            m->setApp(apps_[i].e.nome, cor::doGrupo(apps_[i].corIdx), temMixer ? inst : nullptr, nota);
        }
        posicionarMixers();
        if (pagina_ == 1) repaint(areaPagina_);
    }
    void posicionarMixers() {
        auto area = areaPagina_.reduced(20).withTrimmedTop(32);
        auto fila = area.removeFromTop(36);
        voltarSaidas_.setBounds(fila.removeFromRight(170));
        for (auto* b : escolhaMixer_) {
            if (!b->isVisible()) continue;
            const int w = juce::jmin(170, juce::GlyphArrangement::getStringWidthInt(letra::sans(13.0f, true), b->getButtonText()) + 28);
            b->setBounds(fila.removeFromLeft(w));
            fila.removeFromLeft(6);
        }
        area.removeFromTop(14);
        for (auto* m : mixers_) m->setBounds(area);
    }

    void escolherPeca(int i) {
        const bool batruc = apps_[static_cast<size_t>(i)].e.abertura == "batruc";
        chooser_ = std::make_unique<juce::FileChooser>(T("Escolher peça de") + " " + apps_[static_cast<size_t>(i)].e.nome,
                                                       juce::File(regencia_[i]->peca).getParentDirectory(),
                                                       batruc ? "*.batruc.txt"
                                                       : apps_[static_cast<size_t>(i)].e.abertura == "navalha" ? "*.navalha;*.zip" : "*");
        juce::Component::SafePointer<Palco> self(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [self, i](const juce::FileChooser& fc) {
                                  if (self == nullptr || fc.getResult() == juce::File()) return;
                                  self->regencia_[i]->peca = fc.getResult().getFullPathName();
                                  self->prefs_->setValue("peca_" + self->apps_[static_cast<size_t>(i)].e.nome, fc.getResult().getFullPathName());
                                  self->atualizarRegencia();
                              });
    }

    void desenharMaquina(juce::Graphics& g) {
        const int x = areaApps_.getX() + 18, w = 304;
        int y = areaApps_.getBottom() - 72 - 112;
        g.setColour(cor::trilho);
        g.drawHorizontalLine(y - 10, static_cast<float>(x), static_cast<float>(x + w));
        const int nucleos = juce::SystemStats::getNumCpus();
        double soma = 0.0;
        int atrasos = 0;
        for (auto& a : apps_) soma += a.proc.cpu;
        for (auto& [p, inst] : orq_.instrumentos()) atrasos += inst.atrasos;
        g.setColour(cor::tinta);
        g.setFont(letra::sans(13.0f, true));
        g.drawText(T("máquina"), x, y, 120, 18, juce::Justification::centredLeft);
        g.setFont(letra::mono(13.0f));
        g.drawText(virgula(soma, 2) + " / " + juce::String(nucleos) + " " + T("núcleos"), x + 100, y, w - 100, 18, juce::Justification::centredRight);
        y += 24;
        g.setColour(cor::trilho);
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), 12.0f, 6.0f);
        g.setColour(cor::teal);
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w * juce::jlimit(0.0, 1.0, soma / nucleos)), 12.0f, 6.0f);
        g.setColour(cor::ocre);
        g.fillRect(x + static_cast<int>(w * 0.7), y - 4, 2, 20);
        y += 22;
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(12.0f));
        g.drawText(T("memória") + " " + virgula(memoriaLivreGb(), 1) + " " + T("GB livres"), x, y, 220, 16, juce::Justification::centredLeft);
        g.drawText(T("atrasos") + " " + juce::String(atrasos), x + 200, y, w - 200, 16, juce::Justification::centredRight);
        const bool cabe = soma + 1.0 < 0.7 * nucleos && atrasos == 0;
        g.setColour(cabe ? cor::tealEscuro : cor::vermelhoTexto);
        g.drawFittedText(cabe ? T("Cabe mais um app.") : T("Sem folga: use o orçamento econômico ou feche um app."),
                         x, areaApps_.getBottom() - 32, w, 22, juce::Justification::centredLeft, 2, 0.8f);
    }

    // ——— Saídas ———
    static std::string slug(const juce::String& nome) {
        return nome.toLowerCase().replaceCharacter(' ', '-').retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789-").toStdString();
    }
    AppVivo* appDoStem(int s) {
        for (auto& a : apps_) if (a.stem == s) return &a;
        return nullptr;
    }

    void reconstruirStems() {
        faixas_.clear();
        pistas_.clear();
        const int n = static_cast<int>(host_.stems().size());
        juce::StringArray nomes;
        for (int s = 0; s < n; ++s) {
            auto* app = appDoStem(s);
            const auto nome = app ? app->e.nome : juce::String(host_.stems()[(size_t) s]);
            nomes.add(nome);
            auto* f = faixas_.add(new Faixa(Faixa::Stem));
            addAndMakeVisible(f);
            f->setVisible(pagina_ == 0);
            f->setTextos(nome.toUpperCase(), T("saída do app"), cor::doGrupo(app ? app->corIdx : s), true);
            auto& p = host_.mixer().strip(s);
            f->fader().setDb(p.gainDb.load());
            f->onGanho = [this, s](float db) { host_.mixer().strip(s).gainDb = db; };
            f->onMudo = [this, s](bool m) { host_.mixer().strip(s).mute = m; };
            f->onSolo = [this, s](bool v) { host_.mixer().strip(s).solo = v; };
            f->onRota = [this, s](int r) { host_.mixer().strip(s).route = r; };
            if (app != nullptr) {
                const int ai = static_cast<int>(app - apps_.data());
                f->onMixer = [this, ai] { abrirMixerDe(ai); };
            }
            auto* t = pistas_.add(new juce::ToggleButton());
            addAndMakeVisible(t);
            t->setTitle(T("gravar") + " " + nome);
            t->setVisible(pagina_ == 0);
            t->setToggleState(true, juce::dontSendNotification);
        }
        matriz_.nomes = nomes;
        matriz_.repaint();
        posicionarFaixas();
        posicionarPistas();
        repaint();
    }

    void posicionarFaixas() {
        int x = areaSaidas_.getX() + 20;
        const int y = areaSaidas_.getY() + 52, h = 420;
        for (auto* f : faixas_) { f->setBounds(x, y, 104, h); x += 112; }
        master_->setBounds(areaSaidas_.getRight() - 20 - 112, y, 112, h);
        fones_->setBounds(master_->getX() - 14 - 104, y, 104, h);
    }

    void atualizarAparelhos() {
        if (!host_.running()) return;
        juce::StringArray lista, listaFones;
        if (host_.portable()) {
            // modo portátil: o master escolhe a placa; os fones, se ela tiver
            // mais de duas saídas, vão para as saídas 3 e 4 da mesma placa
            if (auto* tipo = placa_.getCurrentDeviceTypeObject()) lista = tipo->getDeviceNames(false);
            if (auto* dev = placa_.getCurrentAudioDevice(); dev && dev->getOutputChannelNames().size() >= 4)
                listaFones.add(T("saídas 3 e 4"));
        } else {
            for (auto& d : host_.playbackDevices()) lista.add(juce::String(d));
            listaFones = lista;
        }
        if (lista == aparelhos_ && listaFones == aparelhosFones_) return;
        aparelhos_ = lista;
        aparelhosFones_ = listaFones;
        master_->setAparelhos(lista, saidaMaster_);
        fones_->setAparelhos(listaFones, saidaFones_);
    }

    void rotearBarramento(int bus, const juce::String& aparelho) {
        if (host_.portable()) {
            if (bus == 1) {
                fonesNaPlaca_.store(aparelho.isNotEmpty());
            } else if (aparelho.isNotEmpty()) {
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                placa_.getAudioDeviceSetup(setup);
                setup.outputDeviceName = aparelho;
                const auto e = placa_.setAudioDeviceSetup(setup, true);
                if (e.isNotEmpty()) avisar(e);
                else if (auto* dev = placa_.getCurrentAudioDevice(); dev && (int) dev->getCurrentSampleRate() != host_.sampleRate())
                    avisar(T("a placa nova usa outra taxa: feche e abra o Maestruc"));
            }
        } else {
            std::string err;
            if (!host_.routeBus(bus, aparelho.toStdString(), err)) avisar(juce::String(err));
        }
        (bus == 0 ? saidaMaster_ : saidaFones_) = aparelho;
        prefs_->setValue(bus == 0 ? "saida_master" : "saida_fones", aparelho);
    }

    // Modo portátil: a placa pelo JUCE, nos três sistemas. O callback dela
    // chama o motor; master nas saídas 1-2, fones nas 3-4 (se houver e se
    // escolhidos). Sem trava nem alocação no callback.
    struct CallbackPlaca final : juce::AudioIODeviceCallback {
        LiveHost& host;
        std::atomic<bool>& fones;
        CallbackPlaca(LiveHost& h, std::atomic<bool>& f) : host(h), fones(f) {}
        void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int nOut, int n,
                                              const juce::AudioIODeviceCallbackContext&) override {
            for (int c = 0; c < nOut; ++c) if (out[c]) juce::FloatVectorOperations::clear(out[c], n);
            if (nOut < 1 || !out[0]) return;
            float* o[4] = { out[0], nOut > 1 ? out[1] : nullptr, nullptr, nullptr };
            if (fones.load() && nOut >= 4) { o[2] = out[2]; o[3] = out[3]; }
            if (!o[1]) {   // placa mono: mistura L e R
                scratch.resize((size_t) n);   // só cresce na 1ª vez de um bloco maior
                o[1] = scratch.data();
            }
            host.processPortable(o, n);
            if (o[1] == scratch.data()) juce::FloatVectorOperations::addWithMultiply(out[0], o[1], 1.0f, n);
        }
        void audioDeviceAboutToStart(juce::AudioIODevice* dev) override {
            scratch.assign((size_t) juce::jmax(8192, dev->getCurrentBufferSizeSamples()), 0.0f);
        }
        void audioDeviceStopped() override {}
        std::vector<float> scratch;
    };

    bool abrirPortatil(std::string& erro) {
        juce::AudioDeviceManager::AudioDeviceSetup preferido;
        const auto salvo = prefs_->getValue("saida_master");
        const auto e = placa_.initialise(0, 4, nullptr, true);
        if (e.isNotEmpty()) { erro = e.toStdString(); return false; }
        if (salvo.isNotEmpty()) {
            placa_.getAudioDeviceSetup(preferido);
            preferido.outputDeviceName = salvo;
            placa_.setAudioDeviceSetup(preferido, true);   // se não der, fica a padrão
        }
        auto* dev = placa_.getCurrentAudioDevice();
        if (dev == nullptr) { erro = "nenhuma placa de som"; return false; }
        const int taxa = (int) dev->getCurrentSampleRate();
        const int bloco = juce::jmax(8192, dev->getCurrentBufferSizeSamples() * 2);
        if (!host_.openPortable(taxa, bloco, kCapacidade, erro)) return false;
        saidaMaster_ = dev->getName();
        fonesNaPlaca_.store(prefs_->getValue("saida_fones").isNotEmpty() && dev->getOutputChannelNames().size() >= 4);
        if (fonesNaPlaca_.load()) saidaFones_ = T("saídas 3 e 4");
        cbPlaca_ = std::make_unique<CallbackPlaca>(host_, fonesNaPlaca_);
        placa_.addAudioCallback(cbPlaca_.get());
        return true;
    }

    void posicionarPistas() {
        int y = areaGrav_.getY() + 140;
        gravarMaster_.setBounds(areaGrav_.getX() + 20, y + 10, 18, 18);
        y += 38;
        for (auto* t : pistas_) { t->setBounds(areaGrav_.getX() + 20, y + 10, 18, 18); y += 38; }
        gravarFones_.setBounds(areaGrav_.getX() + 20, y + 10, 18, 18);
    }

    void desenharGravacao(juce::Graphics& g) {
        const int x = areaGrav_.getX() + 20, w = 404 - 40;
        int y = areaGrav_.getY() + 18;
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(13.0f, true).withExtraKerningFactor(0.1f));
        g.drawText(T("GRAVAÇÃO"), x, y, 200, 18, juce::Justification::centredLeft);
        if (host_.recording()) {
            g.setColour(cor::vermelhoTexto);
            g.setFont(letra::mono(12.0f));
            g.drawText(u8("\xe2\x97\x8f ") + "take " + juce::String(take_).paddedLeft('0', 2) + u8(" \xc2\xb7 ") + relogio(agora() - gravandoDesde_),
                       x, y, w, 18, juce::Justification::centredRight);
        }
        g.setColour(cor::apagado);
        g.setFont(letra::sans(12.0f));
        g.drawText(T("pasta"), x, y + 30, 100, 16, juce::Justification::centredLeft);
        auto volume = juce::File(pasta_.getText().trim());
        while (!volume.exists() && volume.getParentDirectory() != volume) volume = volume.getParentDirectory();
        const auto livre = volume.getBytesFreeOnVolume();
        const double gb = livre / (1024.0 * 1024.0 * 1024.0);
        const int pistas = static_cast<int>(host_.stems().size()) + 1;
        g.setColour(gb < 10.0 ? cor::vermelhoTexto : cor::tinta2);
        g.drawText(T("disco") + " " + juce::String(static_cast<int>(gb)) + " " + T("GB livres"), x, y + 92, 200, 16, juce::Justification::centredLeft);
        // ~1 GB por pista estéreo de 24 bits a 48 kHz por hora (LEVANTAMENTO_CONEXOES.md)
        g.drawText(T("dá para") + " ~" + juce::String(static_cast<int>(gb / pistas)) + " h", x + 200, y + 92, w - 200, 16, juce::Justification::centredRight);
        y = areaGrav_.getY() + 140;
        auto linha = [&](const juce::String& nome, float nivel, const juce::String& info, bool apagada) {
            g.setColour(cor::trilho);
            g.drawHorizontalLine(y + 37, static_cast<float>(x), static_cast<float>(x + w));
            g.setColour(apagada ? cor::apagado : cor::tinta);
            g.setFont(letra::sans(13.0f, true));
            g.drawFittedText(nome, x + 28, y, 118, 38, juce::Justification::centredLeft, 1, 0.8f);
            auto barra = juce::Rectangle<float>(static_cast<float>(x + 156), y + 15.0f, static_cast<float>(w - 156 - 84), 8.0f);
            g.setColour(cor::trilho);
            g.fillRoundedRectangle(barra, 4.0f);
            g.setColour(apagada ? cor::cinzaBarra : cor::teal);
            g.fillRoundedRectangle(barra.withWidth(barra.getWidth() * nivel), 4.0f);
            g.setColour(cor::tinta2);
            g.setFont(letra::mono(12.0f));
            g.drawFittedText(info, x + w - 80, y, 80, 38, juce::Justification::centredRight, 1, 0.8f);
            y += 38;
        };
        const auto& mm = host_.mixer().masterMeter();
        linha("Master", nivelDeRms(mm.rms.load()), dbDe(mm.rms.load()), false);
        for (int s = 0; s < static_cast<int>(host_.stems().size()); ++s) {
            const auto& m = host_.mixer().stripMeter(s);
            auto* app = appDoStem(s);
            linha(app ? app->e.nome : juce::String(host_.stems()[(size_t) s]), nivelDeRms(m.rms.load()), dbDe(m.rms.load()), false);
        }
        const auto& fm = host_.mixer().fonesMeter();
        linha(T("Fones"), nivelDeRms(fm.rms.load()), T("não grava"), true);
        // Registro.
        auto r = juce::Rectangle<int>(x, areaGrav_.getBottom() - 20 - 110, w, 110);
        g.setColour(cor::fundo);
        g.fillRoundedRectangle(r.toFloat(), 8.0f);
        r.reduce(14, 12);
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(12.0f, true).withExtraKerningFactor(0.08f));
        g.drawText(T("REGISTRO DA SESSÃO"), r.removeFromTop(18), juce::Justification::centredLeft);
        g.setFont(letra::sans(12.0f));
        g.drawFittedText(T("comandos, respostas e acontecimentos de cada app, com o tempo de cada um; vai junto com o take"),
                         r.removeFromTop(40), juce::Justification::topLeft, 3);
        int eventos = 0;
        for (auto& e : orq_.registro()) if (e.tipo == "event" || e.tipo == "obs") ++eventos;
        g.setFont(letra::mono(12.0f));
        g.drawFittedText(juce::String(eventos) + " " + T("acontecimentos") + u8(" \xc2\xb7 ") + juce::String(orq_.comandosEnviados()) + " "
                             + T("comandos") + u8(" \xc2\xb7 ") + juce::String(contarInfluencias()) + " " + T("influências"),
                         r, juce::Justification::centredLeft, 1, 0.8f);
    }

    static juce::String dbDe(float rms) {
        return rms > 1.0e-5f ? textoDb(20.0f * std::log10(rms)) + " dB" : u8("\xe2\x80\x94");
    }

    void alternarGravacao() {
        if (host_.recording()) {
            const bool ok = host_.stopRecording();
            salvarRegistro();
            avisar(ok ? "Take " + juce::String(take_) + ": " + pastaTake_.getFullPathName()
                         + (host_.droppedFrames() ? " (" + juce::String((juce::int64) host_.droppedFrames()) + " " + T("quadros perdidos") + ")" : juce::String())
                      : T("A gravação terminou com erro de disco."));
            gravar_.getProperties().set("icone", "rec");
            traduzir();
            return;
        }
        const auto texto = pasta_.getText().trim();
        if (!juce::File::isAbsolutePath(texto)) { avisar(T("A pasta precisa ser um caminho completo.")); return; }
        ++take_;
        const auto agoraTake = juce::Time::getCurrentTime();
        pastaTake_ = juce::File(texto).getChildFile(agoraTake.formatted("%Y-%m-%d"))
                         .getChildFile("take-" + juce::String(take_).paddedLeft('0', 2) + "-" + agoraTake.formatted("%H%M%S"));
        std::vector<bool> on;
        for (auto* t : pistas_) on.push_back(t->getToggleState());
        std::string err;
        if (!host_.startRecording(pastaTake_.getFullPathName().toStdString(), err, &on, gravarMaster_.getToggleState())) {
            --take_;
            avisar(juce::String(err));
            return;
        }
        registroDesde_ = orq_.registro().size();
        orq_.enviarATodos("", "query");
        gravandoDesde_ = agora();
        gravar_.getProperties().set("icone", "stop");
        aviso_ = {};
        traduzir();
    }

    // O registro do take: quem tocou, com que seed, e o que aconteceu.
    void salvarRegistro() {
        juce::Array<juce::var> itens;
        for (int i = registroDesde_; i < orq_.registro().size(); ++i) {
            const auto& e = orq_.registro().getReference(i);
            auto* o = new juce::DynamicObject();
            o->setProperty("t_us", e.quandoUs);
            o->setProperty("tipo", e.tipo);
            o->setProperty("app", e.nome);
            o->setProperty("dados", e.dados);
            itens.add(juce::var(o));
        }
        auto* raiz = new juce::DynamicObject();
        raiz->setProperty("formato", "maestruc-take");
        raiz->setProperty("versao", 1);
        juce::Array<juce::var> apps, stems;
        for (auto& [p, inst] : orq_.instrumentos()) {
            auto* o = new juce::DynamicObject();
            o->setProperty("nome", inst.nome);
            o->setProperty("versao", inst.versao);
            o->setProperty("seed", inst.seed);
            o->setProperty("bpm", inst.bpm);
            apps.add(juce::var(o));
        }
        for (auto& s : host_.stems()) stems.add(juce::String(s));
        raiz->setProperty("stems", stems);
        raiz->setProperty("apps", apps);
        raiz->setProperty("registro", itens);
        pastaTake_.getChildFile("log.json").replaceWithText(juce::JSON::toString(juce::var(raiz)));
    }

    // ——— Influências ———
    void mostrarInfluencia() {
        auto it = influencias_.find({ matriz_.linha, matriz_.coluna });
        const Influencia inf = it != influencias_.end() ? it->second : Influencia {};
        alvo_.setSelectedId(inf.alvo + 1, juce::dontSendNotification);
        escuta_.setSelectedId(inf.escuta + 1, juce::dontSendNotification);
        quantidade_.setValue(inf.quantidade, juce::dontSendNotification);
        suavidade_.setValue(inf.ms, juce::dontSendNotification);
        repaint(painelInf_);
    }

    void ligarInfluencia(bool ligar) {
        if (matriz_.linha < 0 || matriz_.coluna < 0) { avisar(T("Escolha uma casa da matriz.")); return; }
        auto& inf = influencias_[{ matriz_.linha, matriz_.coluna }];
        if (ligar && inf.slot < 0) {
            for (int k = 0; k < kMaxInfluences; ++k)
                if (!slotUsado(k)) { inf.slot = k; break; }
            if (inf.slot < 0) { avisar(T("Limite de influências atingido.")); return; }
        }
        inf.ativa = ligar;
        aplicarInfluencia();
        matriz_.repaint();
        repaint();
    }

    bool slotUsado(int k) const {
        for (auto& [key, inf] : influencias_) if (inf.slot == k) return true;
        return false;
    }

    void aplicarInfluencia() {
        if (matriz_.linha < 0 || matriz_.coluna < 0) return;
        auto it = influencias_.find({ matriz_.linha, matriz_.coluna });
        if (it == influencias_.end()) return;
        auto& inf = it->second;
        inf.alvo = juce::jmax(0, alvo_.getSelectedId() - 1);
        inf.escuta = juce::jlimit(0, kNumListen - 1, escuta_.getSelectedId() - 1);
        inf.quantidade = static_cast<float>(quantidade_.getValue());
        inf.ms = static_cast<float>(suavidade_.getValue());
        if (inf.slot < 0) return;
        auto& p = host_.mixer().influence(inf.slot);
        p.source = matriz_.linha;
        p.dest = matriz_.coluna == Matriz::kColunaMaster ? kDestMaster : matriz_.coluna;
        p.target = inf.alvo == 1 ? static_cast<int>(Target::Brilho) : static_cast<int>(Target::Ganho);
        p.listen = inf.escuta;
        p.amount = inf.quantidade;
        p.timeMs = inf.ms;
        p.active = inf.ativa;
        matriz_.repaint();
    }

    int contarInfluencias() const {
        int n = 0;
        for (auto& [k, inf] : influencias_) n += inf.ativa ? 1 : 0;
        return n;
    }

    void desenharDetalheInfluencia(juce::Graphics& g) {
        g.setColour(cor::fundo);
        g.fillRoundedRectangle(painelInf_.toFloat(), 8.0f);
        auto r = painelInf_.reduced(16, 14);
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(12.0f, true).withExtraKerningFactor(0.08f));
        g.drawText(T("INFLUÊNCIA ESCOLHIDA"), r.removeFromTop(18), juce::Justification::centredLeft);
        r.removeFromTop(10);
        const int n = matriz_.nomes.size();
        const bool tem = matriz_.linha >= 0 && matriz_.linha < n && (matriz_.coluna == Matriz::kColunaMaster || (matriz_.coluna >= 0 && matriz_.coluna < n));
        const auto destino = matriz_.coluna == Matriz::kColunaMaster ? juce::String("Master") : matriz_.nomes[matriz_.coluna];
        g.setColour(cor::tinta);
        g.setFont(letra::forte(18.0f));
        g.drawFittedText(tem ? matriz_.nomes[matriz_.linha] + u8(" \xe2\x86\x92 ") + destino : T("escolha uma casa da matriz"),
                         r.removeFromTop(24), juce::Justification::centredLeft, 1, 0.8f);
        r.removeFromTop(10);
        auto l = r.removeFromTop(20);
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(12.0f));
        g.drawFittedText(T("o que se escuta na fonte"), l.removeFromLeft(l.getWidth() / 2), juce::Justification::centredLeft, 1, 0.8f);
        g.drawFittedText(T("o que muda no destino"), l.withTrimmedLeft(5), juce::Justification::centredLeft, 1, 0.8f);
        r.removeFromTop(36 + 10);
        g.setFont(letra::sans(13.0f));
        g.drawFittedText(T("quantidade"), r.getX(), r.getY(), 92, 30, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour(cor::tinta);
        g.setFont(letra::mono(13.0f));
        const double q = quantidade_.getValue();
        g.drawText((q > 0 ? juce::String("+") : q < 0 ? menos() : juce::String()) + virgula(std::abs(q), 2), r.getRight() - 64, r.getY(), 64, 30, juce::Justification::centredLeft);
        r.removeFromTop(38);
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(13.0f));
        g.drawFittedText(T("suavidade"), r.getX(), r.getY(), 92, 30, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour(cor::tinta);
        g.setFont(letra::mono(13.0f));
        g.drawText(juce::String(juce::roundToInt(suavidade_.getValue())) + " ms", r.getRight() - 64, r.getY(), 64, 30, juce::Justification::centredLeft);
        r.removeFromTop(42);
        auto tag = r.removeFromTop(24);
        const auto etiqueta = T("NO SOM");
        g.setFont(letra::sans(12.0f, true));
        const int wt = juce::roundToInt(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), etiqueta)) + 18;
        g.setColour(cor::pausa);
        g.fillRoundedRectangle(tag.withWidth(wt).toFloat(), 4.0f);
        g.setColour(cor::pausaTexto);
        g.drawText(etiqueta, tag.withWidth(wt), juce::Justification::centred);
        g.setColour(cor::tinta2);
        g.setFont(letra::sans(12.0f));
        const bool nivel = alvo_.getSelectedId() == 1;
        static const char* const quando[] = { "quando a fonte soa", "a cada ataque da fonte", "quanto mais densa a fonte",
                                              "quando a fonte cala" };
        const auto q0 = T(quando[juce::jlimit(0, kNumListen - 1, escuta_.getSelectedId() - 1)]);
        g.drawFittedText(tem ? q0 + ", " + (nivel ? T("o Maestruc muda o nível do destino (− abre espaço, + sobe)")
                                                   : T("o Maestruc abafa o destino"))
                             : juce::String(),
                         tag.withTrimmedLeft(wt + 10), juce::Justification::centredLeft, 2, 0.8f);
        // Legenda abaixo da matriz.
        auto leg = juce::Rectangle<int>(areaInf_.getX() + 20, areaInf_.getBottom() - 30, areaInf_.getWidth() - 40, 18);
        g.setFont(letra::sans(12.0f));
        g.setColour(cor::ocre);
        g.fillRect(leg.getX(), leg.getY() + 5, 9, 9);
        g.setColour(cor::tinta2);
        g.drawFittedText(T("no som: o Maestruc processa o stem"), leg.getX() + 16, leg.getY(), 300, 18, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour(cor::teal);
        g.fillRect(leg.getX() + 330, leg.getY() + 5, 9, 9);
        g.setColour(cor::tinta2);
        g.drawFittedText(T("na música: o app destino responde — ainda não"), leg.getX() + 346, leg.getY(), 360, 18, juce::Justification::centredLeft, 1, 0.8f);
        g.drawFittedText(T("número = quantidade (− inverte)"), leg.getX() + 720, leg.getY(), 300, 18, juce::Justification::centredLeft, 1, 0.8f);
    }

    // ——— Páginas ———
    void mostrarPagina() {
        const bool saidas = pagina_ == 0;
        for (auto* f : faixas_) f->setVisible(saidas);
        master_->setVisible(saidas);
        fones_->setVisible(saidas);
        for (auto* c : std::initializer_list<juce::Component*> { &matriz_, &alvo_, &escuta_, &quantidade_, &suavidade_, &desligarInf_,
                                                                 &novaInf_, &pasta_, &escolherPasta_, &gravarMaster_, &gravarFones_ })
            c->setVisible(saidas);
        for (auto* t : pistas_) t->setVisible(saidas);
        voltarSaidas_.setVisible(pagina_ == 1);
        atualizarMixers();
        for (auto* l : regencia_) l->setVisible(pagina_ == 2);
        textoAprender_.setVisible(pagina_ == 3);
        textoSobre_.setVisible(pagina_ == 4);
        verificarVersoes_.setVisible(pagina_ == 4);
        for (int i = 0; i < abas_.size(); ++i)
            abas_[i]->setColour(juce::TextButton::textColourOffId, i == pagina_ ? cor::tinta : cor::tinta2);
        repaint();
    }

    void escolherPastaGravacao() {
        chooser_ = std::make_unique<juce::FileChooser>(T("Pasta das gravações"), juce::File(pasta_.getText().trim()));
        juce::Component::SafePointer<Palco> self(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [self](const juce::FileChooser& fc) {
                                  if (self == nullptr || fc.getResult() == juce::File()) return;
                                  self->pasta_.setText(fc.getResult().getFullPathName(), true);
                              });
    }

    // ——— Tempo ———
    void timerCallback() override {
        for (int s = 0; s < faixas_.size(); ++s) faixas_[s]->setNiveis(host_.mixer().stripMeter(s).rms.load());
        master_->setNiveis(host_.mixer().masterMeter().rms.load());
        fones_->setNiveis(host_.mixer().fonesMeter().rms.load());
        if (++ticks_ % 15 == 0) atualizarApps();
        if (pagina_ == 0 && ticks_ % 3 == 0) repaint(areaGrav_);
        if (ticks_ % 30 == 0) repaint(0, 0, kW, 72);
        if (!host_.running() && host_.cycles() > 0 && !avisouQueda_) {
            avisouQueda_ = true;
            avisar(T("O servidor de áudio encerrou o Maestruc."));
        }
    }

    void avisar(const juce::String& texto) { aviso_ = texto; repaint(0, 0, kW, 72); }
    int contarCom(const juce::String& cap) const {
        int n = 0;
        for (auto& [p, i] : orq_.instrumentos()) n += i.tem(cap) ? 1 : 0;
        return n;
    }
    static double agora() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    static juce::String relogio(double s) {
        const int t = static_cast<int>(s);
        return juce::String(t / 60).paddedLeft('0', 2) + ":" + juce::String(t % 60).paddedLeft('0', 2);
    }

    Visual visual_;
    LiveHost host_;
    Orquestra orq_;
    std::unique_ptr<juce::PropertiesFile> prefs_;
    Fonte fonte_ = Fonte::Instalados;
    static constexpr int kAlturaVersoes = 330;
    std::map<juce::String, VersaoPublicada> publicadas_;
    juce::ToggleButton verificarVersoes_;
    versoes::Verificador verificador_;
    juce::String estadoVersoes_;

    // ——— Versão nova (18/11/2026) ———
    void perguntarOuVerificar() {
        if (prefs_->getValue("verificar_versoes").isNotEmpty()) { verificarVersoesSePreciso(); return; }
        juce::Component::SafePointer<Palco> seguro(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Maestruc")
                .withMessage(T("Verificar uma vez por dia, pela internet, se há versão nova dos instrumentos? "
                               "O Maestruc só avisa; nunca baixa nem instala. Dá para mudar depois em SOBRE."))
                .withButton(T("VERIFICAR"))
                .withButton(T("NÃO VERIFICAR"))
                .withAssociatedComponent(this),
            [seguro](int botao) {
                if (seguro == nullptr) return;
                const bool sim = botao == 1;
                seguro->prefs_->setValue("verificar_versoes", sim ? "sim" : "nao");
                seguro->verificarVersoes_.setToggleState(sim, juce::dontSendNotification);
                if (sim) seguro->verificarVersoesSePreciso();
            });
    }

    // No máximo uma rodada por dia; uma falha conta como rodada (sem repetir).
    void verificarVersoesSePreciso() {
        if (prefs_->getValue("verificar_versoes") != "sim" || verificador_.ocupado()) return;
        juce::int64 quando = 0;
        versoes::lerGuardado(prefs_->getValue("versoes_guardadas"), quando);
        const auto agoraMs = juce::Time::currentTimeMillis();
        if (quando > 0 && agoraMs >= quando && agoraMs - quando < versoes::kIntervaloMs) return;
        juce::StringArray repos;
        for (auto& a : apps_)
            if (versoes::repositorioValido(a.e.repositorio)) repos.addIfNotAlreadyThere(a.e.repositorio);
        if (repos.isEmpty()) return;
        // a data vai antes da consulta: fechar no meio não faz repetir
        prefs_->setValue("versoes_guardadas", versoes::guardar(agoraMs, publicadas_));
        estadoVersoes_ = T("verificando…");
        repaint(areaPagina_);
        juce::Component::SafePointer<Palco> seguro(this);
        verificador_.rodar(repos, [seguro, agoraMs](std::map<juce::String, VersaoPublicada> achadas, int falhas) {
            if (seguro == nullptr) return;
            for (auto& [repo, v] : achadas) seguro->publicadas_[repo] = v;
            seguro->prefs_->setValue("versoes_guardadas", versoes::guardar(agoraMs, seguro->publicadas_));
            seguro->estadoVersoes_ = falhas == 0 ? juce::String()
                                                 : juce::String(falhas) + " " + T("sem resposta (sem internet?); nova tentativa amanhã");
            seguro->atualizarCartoes();
            seguro->repaint(seguro->areaPagina_);
        });
    }

    void desenharVersoes(juce::Graphics& g, juce::Rectangle<int> r) {
        juce::int64 quando = 0;
        versoes::lerGuardado(prefs_->getValue("versoes_guardadas"), quando);
        g.setFont(letra::sans(12.0f));
        g.setColour(cor::tinta2);
        auto info = r.removeFromTop(18);
        juce::String linha = quando > 0 ? T("última verificação: ") + juce::Time(quando).formatted("%d/%m/%Y %H:%M")
                                        : T("ainda não verificado");
        if (estadoVersoes_.isNotEmpty()) linha << u8(" \xc2\xb7 ") << estadoVersoes_;
        g.drawText(linha, info, juce::Justification::centredLeft);
        r.removeFromTop(8);
        const int col[] = { 0, 220, 380, 540 };
        auto cab = r.removeFromTop(20);
        g.setFont(letra::sans(11.0f, true));
        g.setColour(cor::apagado);
        const char* t[] = { "INSTRUMENTO", "INSTALADA", "MAIS NOVA" };
        for (int k = 0; k < 3; ++k) g.drawText(T(t[k]), cab.getX() + col[k], cab.getY(), 200, 20, juce::Justification::centredLeft);
        for (auto& a : apps_) {
            if (r.getHeight() < 20) break;
            auto l = r.removeFromTop(22);
            const auto it = publicadas_.find(a.e.repositorio);
            const auto nova = it != publicadas_.end() ? it->second.versao : juce::String();
            const auto inst = a.e.achado() ? (a.e.versaoInstalada.isNotEmpty() ? versoes::limpar(a.e.versaoInstalada) : juce::String("?"))
                                           : T("não instalado");
            g.setFont(letra::sans(13.0f, true));
            g.setColour(cor::tinta);
            g.drawText(a.e.nome, l.getX() + col[0], l.getY(), 210, 22, juce::Justification::centredLeft);
            g.setFont(letra::mono(12.0f));
            g.setColour(cor::tinta2);
            g.drawText(inst, l.getX() + col[1], l.getY(), 150, 22, juce::Justification::centredLeft);
            g.drawText(nova.isNotEmpty() ? nova : u8("\xe2\x80\x94"), l.getX() + col[2], l.getY(), 150, 22,
                       juce::Justification::centredLeft);
            if (a.e.achado() && versoes::haVersaoNova(a.e.versaoInstalada, nova)) {
                g.setFont(letra::sans(12.0f, true));
                g.setColour(cor::tealEscuro);
                g.drawText(T("versão nova: botão VERSÃO NOVA no cartão"), l.getX() + col[3], l.getY(), 400, 22,
                           juce::Justification::centredLeft);
            }
        }
    }
    std::vector<AppVivo> apps_;
    juce::OwnedArray<CartaoApp> cartoes_;
    juce::AudioDeviceManager placa_;               // só no modo portátil
    std::unique_ptr<CallbackPlaca> cbPlaca_;
    std::atomic<bool> fonesNaPlaca_ { false };
    juce::StringArray aparelhosFones_;
    juce::Viewport rolagemApps_;
    juce::Component listaApps_;
    juce::OwnedArray<LinhaRegencia> regencia_;
    juce::OwnedArray<MixerDoApp> mixers_;
    juce::OwnedArray<juce::TextButton> escolhaMixer_;
    juce::TextButton voltarSaidas_;
    int mixersVisiveis_ = 0, mixerEscolhido_ = -1;
    juce::OwnedArray<juce::TextButton> abas_, orcamento_;
    juce::OwnedArray<Faixa> faixas_;
    juce::OwnedArray<juce::ToggleButton> pistas_;
    std::unique_ptr<Faixa> master_, fones_;
    Matriz matriz_;
    std::map<std::pair<int, int>, Influencia> influencias_;
    juce::ComboBox alvo_, escuta_, lingua_;
    juce::Slider quantidade_, suavidade_;
    juce::TextButton tocar_, parar_, gravar_, desligarInf_, novaInf_, escolherPasta_;
    juce::Label tempo_;
    juce::TextEditor pasta_, textoAprender_, textoSobre_;
    juce::ToggleButton gravarMaster_, gravarFones_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Rectangle<int> areaApps_, areaSaidas_, areaInf_, areaGrav_, areaPagina_, painelInf_;
    juce::StringArray aparelhos_;
    juce::String aviso_, saidaMaster_, saidaFones_;
    juce::File pastaTake_;
    int pagina_ = 0, take_ = 0, ticks_ = 0, registroDesde_ = 0, verificacoes_ = 0;
    double inicio_ = 0.0, gravandoDesde_ = 0.0;
    bool avisouQueda_ = false;
};

// Envolve o palco de 1920 × 1080 e o escala para o tamanho da janela.
class Moldura final : public juce::Component {
public:
    Moldura() { addAndMakeVisible(palco_); }
    void paint(juce::Graphics& g) override { g.fillAll(cor::fundo); }
    void resized() override {
        const float s = juce::jmin(getWidth() / static_cast<float>(Palco::kW), getHeight() / static_cast<float>(Palco::kH));
        const float dx = (getWidth() - Palco::kW * s) * 0.5f, dy = (getHeight() - Palco::kH * s) * 0.5f;
        palco_.setTransform(juce::AffineTransform::scale(s).translated(dx, dy));
    }

private:
    Palco palco_;
};

} // namespace maestruc
