// Maestruc — o regente da família RASGO: abre os instrumentos, recebe o som
// de cada um, mixa, faz um influenciar o outro, rege seeds e peças, grava.
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#include "Palco.h"

#include <csignal>

namespace {

// Ctrl-C ou kill: sair pelo caminho normal, que devolve os apps às caixas.
std::atomic<bool> g_sinal { false };
void aoSinal(int) { g_sinal = true; }

class Janela final : public juce::DocumentWindow {
public:
    Janela() : DocumentWindow("Maestruc", maestruc::cor::fundo, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new maestruc::Moldura(), false);
        setResizable(true, true);
        setResizeLimits(960, 540, 3840, 2160);
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds;
        const float s = juce::jmin(1.0f, area.getWidth() * 0.92f / 1920.0f, area.getHeight() * 0.9f / 1080.0f);
        centreWithSize(juce::roundToInt(1920 * s), juce::roundToInt(1080 * s));
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class MaestrucApp final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Maestruc"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override {
        std::signal(SIGINT, aoSinal);
        std::signal(SIGTERM, aoSinal);
        janela_ = std::make_unique<Janela>();
        vigia_.onTick = [this] { if (g_sinal) systemRequestedQuit(); };
        vigia_.startTimer(200);
    }
    void shutdown() override { vigia_.stopTimer(); janela_.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    struct Vigia final : juce::Timer {
        std::function<void()> onTick;
        void timerCallback() override { if (onTick) onTick(); }
    } vigia_;
    std::unique_ptr<Janela> janela_;
};

} // namespace

START_JUCE_APPLICATION(MaestrucApp)
