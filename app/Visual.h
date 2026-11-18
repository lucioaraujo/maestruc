// Maestruc — cores, letras e desenho dos controles, conforme o layout
// aprovado pelo autor (claude.ai/artifact/2FocMsixcbdCoZ4t7g17Ns, 14/11/2026).
// Copyright (c) 2026 Lúcio de Araújo — AGPL-3.0-or-later.
#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace maestruc::cor {
inline const juce::Colour fundo { 0xffF2EDE3 }, painel { 0xffFBF8F2 }, borda { 0xffD9D0C0 },
    bordaControle { 0xffB9AE9C }, tinta { 0xff23201B }, tinta2 { 0xff4A443B }, apagado { 0xff6B6358 },
    teal { 0xff1D6670 }, tealEscuro { 0xff134850 }, ocre { 0xff9A6A1F }, vermelho { 0xffB3362B },
    vermelhoTexto { 0xff9E2E24 }, creme { 0xffFFFDF8 }, trilho { 0xffEFE8DA }, pausa { 0xffEFE3CB },
    pausaTexto { 0xff6E4C16 }, masterBorda { 0xffC9B48C }, tagMusica { 0xffE3EFEF }, cinzaBarra { 0xff8C8170 };

inline juce::Colour doGrupo(int i) {
    static const juce::Colour c[] = { teal, ocre, tinta2, tealEscuro, pausaTexto, apagado };
    return c[static_cast<std::size_t>(i) % 6];
}
} // namespace maestruc::cor

namespace maestruc::letra {
// IBM Plex Sans (texto) e IBM Plex Mono (números), como no layout aprovado;
// SIL OFL 1.1, proveniência em app/fonts/PROVENIENCIA.md.
inline juce::Typeface::Ptr face(const void* data, size_t size) { return juce::Typeface::createSystemTypefaceFor(data, size); }
inline juce::Font sans(float h, bool negrito = false) {
    static auto reg = face(BinaryData::IBMPlexSansRegular_ttf, BinaryData::IBMPlexSansRegular_ttfSize);
    static auto semi = face(BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize);
    return juce::Font(juce::FontOptions().withTypeface(negrito ? semi : reg).withHeight(h));
}
inline juce::Font forte(float h) {   // 700: título, nomes de app
    static auto bold = face(BinaryData::IBMPlexSansBold_ttf, BinaryData::IBMPlexSansBold_ttfSize);
    return juce::Font(juce::FontOptions().withTypeface(bold).withHeight(h));
}
inline juce::Font medio(float h) {   // 500: abas não escolhidas
    static auto med = face(BinaryData::IBMPlexSansMedium_ttf, BinaryData::IBMPlexSansMedium_ttfSize);
    return juce::Font(juce::FontOptions().withTypeface(med).withHeight(h));
}
inline juce::Font mono(float h, bool negrito = false) {
    static auto reg = face(BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    static auto bold = face(BinaryData::IBMPlexMonoBold_ttf, BinaryData::IBMPlexMonoBold_ttfSize);
    return juce::Font(juce::FontOptions().withTypeface(negrito ? bold : reg).withHeight(h));
}
} // namespace maestruc::letra

namespace maestruc {

// Propriedades de um botão: "borda" (cor), "raio".
inline void estiloBotao(juce::TextButton& b, juce::Colour fundo, juce::Colour texto, juce::Colour borda, float raio = 4.0f) {
    b.setColour(juce::TextButton::buttonColourId, fundo);
    b.setColour(juce::TextButton::buttonOnColourId, fundo);
    b.setColour(juce::TextButton::textColourOffId, texto);
    b.setColour(juce::TextButton::textColourOnId, texto);
    b.getProperties().set("borda", static_cast<juce::int64>(borda.getARGB()));
    b.getProperties().set("raio", raio);
}

class Visual final : public juce::LookAndFeel_V4 {
public:
    Visual() {
        setColour(juce::PopupMenu::backgroundColourId, cor::painel);
        setColour(juce::PopupMenu::textColourId, cor::tinta);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, cor::teal);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::ComboBox::textColourId, cor::tinta);
        setColour(juce::ComboBox::backgroundColourId, cor::painel);
        setColour(juce::ComboBox::outlineColourId, cor::bordaControle);
        setColour(juce::TextEditor::backgroundColourId, cor::painel);
        setColour(juce::TextEditor::textColourId, cor::tinta);
        setColour(juce::TextEditor::outlineColourId, cor::bordaControle);
        setColour(juce::TextEditor::focusedOutlineColourId, cor::teal);
        setColour(juce::TextEditor::highlightColourId, cor::teal.withAlpha(0.25f));
        setColour(juce::CaretComponent::caretColourId, cor::tinta);
        setColour(juce::Label::textColourId, cor::tinta);
        setColour(juce::Label::textWhenEditingColourId, cor::tinta);
        setColour(juce::Label::backgroundWhenEditingColourId, cor::painel);
        setColour(juce::Label::outlineWhenEditingColourId, cor::teal);
        setColour(juce::ToggleButton::textColourId, cor::tinta);
        setColour(juce::ScrollBar::thumbColourId, cor::bordaControle);
        setColour(juce::AlertWindow::backgroundColourId, cor::painel);
        setColour(juce::AlertWindow::textColourId, cor::tinta);
        setColour(juce::AlertWindow::outlineColourId, cor::borda);
        setColour(juce::ResizableWindow::backgroundColourId, cor::fundo);
        setColour(juce::DocumentWindow::textColourId, cor::tinta);
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& f) override {
        // Fonte sem nome (padrão do JUCE): Plex Sans, para nada cair em outra letra.
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return letra::sans(f.getHeight(), f.isBold()).getTypefacePtr();
        return LookAndFeel_V4::getTypefaceForFont(f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& fill, bool over, bool down) override {
        const float raio = static_cast<float>(b.getProperties().getWithDefault("raio", 4.0f));
        auto r = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = b.getToggleState() ? b.findColour(juce::TextButton::buttonOnColourId) : fill;
        if (!b.isEnabled()) c = c.interpolatedWith(cor::trilho, 0.6f);
        else if (down) c = c.darker(0.12f);
        else if (over) c = c.brighter(0.06f).interpolatedWith(cor::creme, 0.08f);
        g.setColour(c);
        g.fillRoundedRectangle(r, raio);
        const auto argb = b.getProperties()["borda"];
        if (!argb.isVoid()) {
            g.setColour(juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(argb))).withMultipliedAlpha(b.isEnabled() ? 1.0f : 0.5f));
            g.drawRoundedRectangle(r, raio, b.getProperties().getWithDefault("bordaGrossa", false) ? 3.0f : 1.0f);
        }
        if (b.hasKeyboardFocus(false)) {
            g.setColour(cor::teal);
            g.drawRoundedRectangle(r.reduced(2.0f), raio, 2.0f);
        }
    }

    juce::Font getTextButtonFont(juce::TextButton& b, int h) override {
        const float tamanho = static_cast<float>(b.getProperties().getWithDefault("letra", juce::jmin(15.0f, h * 0.42f)));
        return b.getProperties().getWithDefault("mono", false) ? letra::mono(tamanho, true) : letra::sans(tamanho, true);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(getTextButtonFont(b, b.getHeight()));
        auto c = b.findColour(b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        g.setColour(b.isEnabled() ? c : c.withMultipliedAlpha(0.45f));
        auto r = b.getLocalBounds().reduced(6, 0);
        // Ícone opcional à esquerda: "play", "stop", "rec".
        const auto icone = b.getProperties()["icone"].toString();
        if (icone.isNotEmpty()) {
            const auto texto = b.getButtonText();
            const float wTexto = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), texto);
            const float total = 14.0f + 10.0f + wTexto;
            float x = r.getCentreX() - total / 2.0f;
            const float cy = r.getCentreY();
            juce::Path p;
            if (icone == "play") p.addTriangle(x, cy - 8, x + 13, cy, x, cy + 8);
            else if (icone == "stop") p.addRectangle(x, cy - 6, 12, 12);
            else p.addEllipse(x, cy - 6, 12, 12);
            g.setColour(juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(b.getProperties().getWithDefault("corIcone", static_cast<juce::int64>(c.getARGB()))))));
            g.fillPath(p);
            g.setColour(b.isEnabled() ? c : c.withMultipliedAlpha(0.45f));
            g.drawText(texto, juce::Rectangle<float>(x + 24.0f, r.getY(), wTexto + 4.0f, static_cast<float>(r.getHeight())), juce::Justification::centredLeft, false);
            return;
        }
        g.drawFittedText(b.getButtonText(), r, juce::Justification::centred, 2, 0.9f);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override {
        auto r = juce::Rectangle<float>(0, 0, static_cast<float>(w), static_cast<float>(h)).reduced(0.5f);
        g.setColour(box.isEnabled() ? cor::painel : cor::trilho);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(box.hasKeyboardFocus(true) ? cor::teal : cor::bordaControle);
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
        juce::Path seta;
        const float x = w - 14.0f, y = h * 0.5f;
        seta.addTriangle(x - 4, y - 2, x + 4, y - 2, x, y + 3);
        g.setColour(cor::tinta2);
        g.fillPath(seta);
    }
    juce::Font getComboBoxFont(juce::ComboBox& b) override { return letra::sans(juce::jmin(13.0f, b.getHeight() * 0.45f)); }
    juce::Font getPopupMenuFont() override { return letra::sans(14.0f); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(4, 1, box.getWidth() - 22, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                          juce::Slider::SliderStyle, juce::Slider& s) override {
        const float cy = y + h * 0.5f;
        g.setColour(cor::borda);
        g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(w), 4.0f, 2.0f);
        // Preenchimento a partir do centro quando o intervalo é simétrico.
        const bool simetrico = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float zero = simetrico ? static_cast<float>(x + w * (0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : static_cast<float>(x);
        g.setColour(s.isEnabled() ? cor::teal : cor::bordaControle);
        g.fillRoundedRectangle(juce::jmin(zero, pos), cy - 2.0f, std::abs(pos - zero), 4.0f, 2.0f);
        g.setColour(s.isEnabled() ? cor::teal : cor::bordaControle);
        g.fillEllipse(pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
        g.setColour(cor::creme);
        g.fillEllipse(pos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    }

    void drawTickBox(juce::Graphics& g, juce::Component& c, float x, float y, float w, float h, bool ticked,
                     bool enabled, bool, bool) override {
        auto r = juce::Rectangle<float>(x, y, w, h).reduced(1.0f);
        const auto acento = juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(c.getProperties().getWithDefault("acento", static_cast<juce::int64>(cor::vermelho.getARGB())))));
        g.setColour(ticked ? acento : cor::painel);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(ticked ? acento : cor::bordaControle);
        g.drawRoundedRectangle(r, 3.0f, 1.2f);
        if (ticked) {
            juce::Path p;
            p.startNewSubPath(r.getX() + r.getWidth() * 0.22f, r.getCentreY());
            p.lineTo(r.getX() + r.getWidth() * 0.42f, r.getBottom() - r.getHeight() * 0.25f);
            p.lineTo(r.getRight() - r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.25f);
            g.setColour(enabled ? juce::Colours::white : cor::trilho);
            g.strokePath(p, juce::PathStrokeType(2.0f));
        }
    }
};

} // namespace maestruc
