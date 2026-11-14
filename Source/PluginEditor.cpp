#include "PluginEditor.h"

using namespace bateriasep;

namespace
{
float dbToPos (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }   // -60..+6 dB -> 0..1

juce::String dbText (float db) { return db <= -59.9f ? juce::String (juce::CharPointer_UTF8 ("-\xE2\x88\x9E dB")) : juce::String (db, 1) + " dB"; }

juce::Font bold (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }

// "pintura" de metal/plástico de uma tecla ou tampa: degradê vertical com borda e brilho
void drawCap (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, float corner)
{
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.8f), corner);
    g.setGradientFill (juce::ColourGradient (base.brighter (0.28f), 0.0f, r.getY(), base.darker (0.14f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (ui::ink.withAlpha (0.65f));
    g.drawRoundedRectangle (r, corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawLine (r.getX() + corner, r.getY() + 1.5f, r.getRight() - corner, r.getY() + 1.5f, 1.0f);
}
}

// =================================================================================================
void ui::setSkin (int i)
{
    skin = i < 0 ? 0 : (i > 2 ? 2 : i);
    using juce::Colour;
    if (skin == 0)          // Moderno escuro (padrão)
    {
        bg = Colour (0xff14161b);      panel = Colour (0xff1e2128);    panel2 = Colour (0xff272b35);
        ink = Colour (0xffe8eaed);     textMain = Colour (0xffe8eaed); textDim = Colour (0xff8b92a0);
        accent = Colour (0xff4fd1c5);  accentDeep = Colour (0xff2f7772); amber = Colour (0xfff2b84b); red = Colour (0xffe5645a);
        display = Colour (0xff0f1115); displayText = Colour (0xffe8eaed); displayDim = Colour (0xff8b92a0);
        led = Colour (0xff4fd1c5);     tick = Colour (0xffffffff);     playhead = Colour (0xffffffff);
        onAccent = Colour (0xff0b1214); capColour = Colour (0xffdfe1e6);
        const Colour sc[6] = { Colour (0xffe5645a), Colour (0xfff2b84b), Colour (0xff9b7be8), Colour (0xff4fd1c5), Colour (0xff6aa8f5), Colour (0xff7fd36b) };
        for (int k = 0; k < 6; ++k) stemColours[k] = sc[k];
    }
    else if (skin == 1)     // Mesa SSL 9000: off-white + laranja
    {
        bg = Colour (0xffe9e3d3);      panel = Colour (0xffd9d2bf);    panel2 = Colour (0xfff3eee1);
        ink = Colour (0xff1e1c19);     textMain = Colour (0xff1e1c19); textDim = Colour (0xff6f695b);
        accent = Colour (0xffe8731a);  accentDeep = Colour (0xffb8560b); amber = Colour (0xffd9962b); red = Colour (0xffc8453b);
        display = Colour (0xff131110); displayText = Colour (0xfff0e6cf); displayDim = Colour (0xff8d8573);
        led = Colour (0xffff9a2e);     tick = Colour (0xffffffff);     playhead = Colour (0xfffff3d6);
        onAccent = Colour (0xff1e1c19); capColour = Colour (0xffe6e0cf);
        wood1 = Colour (0xff5b3a27);   wood2 = Colour (0xff351f14);
        const Colour sc[6] = { Colour (0xffc8453b), Colour (0xffd9962b), Colour (0xff7e63c9), Colour (0xff2e9e94), Colour (0xff3f7fd1), Colour (0xff5fa33a) };
        for (int k = 0; k < 6; ++k) stemColours[k] = sc[k];
    }
    else                    // Estúdio claro
    {
        bg = Colour (0xffe9ecf2);      panel = Colour (0xffffffff);    panel2 = Colour (0xfff2f4f9);
        ink = Colour (0xff1f2430);     textMain = Colour (0xff1f2430); textDim = Colour (0xff68707f);
        accent = Colour (0xff4f46e5);  accentDeep = Colour (0xff8f8af0); amber = Colour (0xffd98a00); red = Colour (0xffd9483b);
        display = Colour (0xffe6e9f1); displayText = Colour (0xff1f2430); displayDim = Colour (0xff68707f);
        led = Colour (0xff4f46e5);     tick = Colour (0xff1f2430);     playhead = Colour (0xff1f2430);
        onAccent = Colour (0xffffffff); capColour = Colour (0xffffffff);
        const Colour sc[6] = { Colour (0xffd9483b), Colour (0xffe0a100), Colour (0xff7c5ce0), Colour (0xff14a39a), Colour (0xff3b82f6), Colour (0xff3fa34d) };
        for (int k = 0; k < 6; ++k) stemColours[k] = sc[k];
    }
}

BsLookAndFeel::BsLookAndFeel() { updateColours(); }

void BsLookAndFeel::updateColours()
{
    const bool an = ui::analog(), darkFlat = ! an && ! ui::light();
    setColour (juce::TextButton::buttonColourId, ui::panel2);
    setColour (juce::TextButton::buttonOnColourId, ui::accent);
    setColour (juce::TextButton::textColourOffId, ui::ink);
    setColour (juce::TextButton::textColourOnId, ui::ink);
    setColour (juce::ToggleButton::textColourId, ui::ink);
    setColour (juce::ToggleButton::tickColourId, ui::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, ui::textDim);
    setColour (juce::ComboBox::backgroundColourId, an ? ui::display : ui::panel2);
    setColour (juce::ComboBox::textColourId, an ? ui::displayText : ui::textMain);
    setColour (juce::ComboBox::outlineColourId, an ? ui::ink : ui::textDim.withAlpha (0.4f));
    setColour (juce::ComboBox::arrowColourId, an ? ui::led : ui::textDim);
    setColour (juce::PopupMenu::backgroundColourId, ui::panel2);
    setColour (juce::PopupMenu::textColourId, ui::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, ui::onAccent);
    setColour (juce::ProgressBar::backgroundColourId, an ? juce::Colour (0xff2a2620) : ui::display);
    setColour (juce::ProgressBar::foregroundColourId, ui::led);
    setColour (juce::Label::textColourId, ui::ink);
    setColour (juce::TooltipWindow::backgroundColourId, darkFlat ? ui::panel2 : ui::ink);
    setColour (juce::TooltipWindow::textColourId, darkFlat ? ui::textMain : ui::bg);
    setColour (juce::TooltipWindow::outlineColourId, ui::accent);
    setColour (juce::Slider::rotarySliderFillColourId, ui::ink);
}

void BsLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> c, float r)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff0ece0), c.x - r, c.y - r, juce::Colour (0xff8c877a), c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colour (0xff1e1c19).withAlpha (0.7f));
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 0.8f);
    g.drawLine (c.x - r * 0.65f, c.y + r * 0.3f, c.x + r * 0.65f, c.y - r * 0.3f, 1.3f);
}

int BsLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    return s.isVertical() ? (ui::analog() ? 24 : 11) : LookAndFeel_V4::getSliderThumbRadius (s);
}

juce::Font BsLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return bold (12.0f); }
juce::Font BsLookAndFeel::getComboBoxFont (juce::ComboBox&) { return bold (12.0f); }
juce::Font BsLookAndFeel::getPopupMenuFont() { return juce::Font (juce::FontOptions (13.5f)); }

void BsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f, 1.0f);
    if (! ui::analog())
    {
        auto base = bg;
        if (! b.isEnabled()) base = base.withAlpha (0.5f);
        else if (down) base = base.darker (0.2f);
        else if (highlighted) base = base.brighter (0.12f);
        g.setColour (base);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (ui::light() ? ui::ink.withAlpha (0.14f) : juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        return;
    }
    auto base = bg;
    if (! b.isEnabled()) base = base.withMultipliedSaturation (0.4f).interpolatedWith (ui::bg, 0.45f);
    else if (down) base = base.darker (0.22f);
    else if (highlighted) base = base.brighter (0.07f);
    if (down) r = r.translated (0.0f, 1.0f);
    drawCap (g, r, base, 3.0f);
}

void BsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    if (! ui::analog())
    {
        const float sz = 16.0f;
        juce::Rectangle<float> box (bounds.getX() + 2.0f, bounds.getCentreY() - sz * 0.5f, sz, sz);
        const float al = b.isEnabled() ? 1.0f : 0.5f;
        const bool on2 = b.getToggleState();
        g.setColour (on2 ? ui::accent.withAlpha (al) : ui::panel2.withAlpha (al));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour ((on2 ? ui::accent : ui::textDim).withAlpha (0.6f * al));
        g.drawRoundedRectangle (box, 4.0f, 1.2f);
        if (on2)
        {
            juce::Path tk;
            tk.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
            tk.lineTo (box.getX() + 7.0f, box.getBottom() - 4.5f);
            tk.lineTo (box.getRight() - 3.5f, box.getY() + 4.5f);
            g.setColour (ui::onAccent.withAlpha (al));
            g.strokePath (tk, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (ui::ink.withAlpha (al));
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawFittedText (b.getButtonText(), bounds.withTrimmedLeft (sz + 9.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
        return;
    }
    const float s = 14.0f;
    juce::Rectangle<float> led (bounds.getX() + 2.0f, bounds.getCentreY() - s * 0.5f, s, s);
    const bool on = b.getToggleState();
    const float alpha = b.isEnabled() ? 1.0f : 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (led.translated (0.0f, 1.2f), 2.5f);
    g.setColour (on ? ui::led.withAlpha (alpha) : juce::Colour (0xff3a2a1c).withAlpha (alpha));
    g.fillRoundedRectangle (led, 2.5f);
    if (on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f * alpha));
        g.fillRoundedRectangle (led.reduced (3.0f, 3.0f).withHeight (3.0f), 1.5f);
    }
    g.setColour (ui::ink.withAlpha (highlighted ? 1.0f : 0.8f));
    g.drawRoundedRectangle (led, 2.5f, 1.1f);

    g.setColour (ui::ink.withAlpha (alpha));
    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    g.drawFittedText (b.getButtonText(), bounds.withTrimmedLeft (s + 8.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
}

void BsLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    if (! ui::analog())
    {
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        juce::Path ch;
        const float cx2 = (float) w - 14.0f, cy2 = (float) h * 0.5f;
        ch.startNewSubPath (cx2 - 4.0f, cy2 - 2.0f); ch.lineTo (cx2, cy2 + 2.5f); ch.lineTo (cx2 + 4.0f, cy2 - 2.0f);
        g.setColour (box.isEnabled() ? ui::textDim : ui::textDim.withAlpha (0.4f));
        g.strokePath (ch, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }
    g.setColour (ui::display);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (r.getX() + 3, r.getBottom() - 1, r.getRight() - 3, r.getBottom() - 1);
    g.setColour (ui::ink);
    g.drawRoundedRectangle (r, 3.0f, 1.2f);

    const float cx = (float) w - 11.0f, cy = (float) h * 0.5f;
    juce::Path p;
    p.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (box.isEnabled() ? ui::led : ui::displayDim);
    g.fillPath (p);
}

void BsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s)
{
    const float d = (float) std::min (w, h);
    const auto c = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).getCentre();
    if (! ui::analog())
    {
        const float Rr = d * 0.5f, tr = Rr * 0.84f;
        const float al = s.isEnabled() ? 1.0f : 0.45f;
        const float stroke = std::max (2.4f, d * 0.09f);
        juce::Path track, value;
        track.addCentredArc (c.x, c.y, tr, tr, 0.0f, startAngle, endAngle, true);
        g.setColour (ui::textDim.withAlpha (0.28f * al));
        g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const double mn = s.getMinimum(), mx = s.getMaximum();
        const bool bipolar = mn < 0.0 && mx > 0.0;
        const float zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
        const float a0 = startAngle + zeroPos * (endAngle - startAngle), a1 = startAngle + pos * (endAngle - startAngle);
        if (std::abs (a1 - a0) > 0.01f)
        {
            value.addCentredArc (c.x, c.y, tr, tr, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
            g.setColour (ui::accent.withAlpha (al));
            g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const float rb = tr * 0.66f;
        g.setColour (ui::panel2.withAlpha (al));
        g.fillEllipse (c.x - rb, c.y - rb, 2 * rb, 2 * rb);
        g.setColour (ui::textDim.withAlpha (0.45f * al));
        g.drawEllipse (c.x - rb, c.y - rb, 2 * rb, 2 * rb, 1.0f);
        const float ang = startAngle + pos * (endAngle - startAngle);
        const juce::Point<float> dr (std::sin (ang), -std::cos (ang));
        const auto p0 = c + dr * (rb * 0.25f), p1 = c + dr * (rb * 0.92f);
        g.setColour (ui::textMain.withAlpha (al));
        g.drawLine (p0.x, p0.y, p1.x, p1.y, std::max (1.6f, d * 0.065f));
        return;
    }
    const float R = d * 0.5f, rCap = R * 0.72f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float alpha = s.isEnabled() ? 1.0f : 0.5f;
    auto dir = [] (float a) { return juce::Point<float> (std::sin (a), -std::cos (a)); };

    // marcas serigrafadas em volta
    const int nt = 11;
    for (int i = 0; i < nt; ++i)
    {
        const float a = startAngle + (float) i * (endAngle - startAngle) / (float) (nt - 1);
        const bool major = i == 0 || i == nt - 1 || i == (nt - 1) / 2;
        const auto p1 = c + dir (a) * (R * (major ? 0.84f : 0.89f)), p2 = c + dir (a) * R;
        g.setColour (ui::ink.withAlpha (0.8f * alpha));
        g.drawLine (p1.x, p1.y, p2.x, p2.y, major ? 1.3f : 0.9f);
    }

    const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
    g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
    g.fillEllipse (c.x - rCap, c.y - rCap + 1.8f, 2 * rCap, 2 * rCap);                                   // sombra
    g.setGradientFill (juce::ColourGradient (col.darker (0.35f), c.x - rCap, c.y - rCap, col.darker (0.95f), c.x + rCap, c.y + rCap, false));
    g.fillEllipse (c.x - rCap, c.y - rCap, 2 * rCap, 2 * rCap);                                          // saia
    const float r2 = rCap * 0.84f;
    g.setGradientFill (juce::ColourGradient (col.brighter (0.40f), c.x - r2 * 0.6f, c.y - r2 * 0.9f, col.darker (0.20f), c.x + r2 * 0.7f, c.y + r2, false));
    g.fillEllipse (c.x - r2, c.y - r2, 2 * r2, 2 * r2);                                                  // tampa
    g.setColour (juce::Colours::white.withAlpha (0.28f * alpha));
    g.drawEllipse (c.x - r2, c.y - r2, 2 * r2, 2 * r2, 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
    g.drawEllipse (c.x - rCap, c.y - rCap, 2 * rCap, 2 * rCap, 0.8f);

    const bool lightCap = col.getPerceivedBrightness() > 0.6f;
    g.setColour ((lightCap ? ui::ink : juce::Colour (0xfff7f2e6)).withAlpha (alpha));
    const auto a0 = c + dir (angle) * (r2 * 0.22f), a1 = c + dir (angle) * (r2 * 0.97f);
    g.drawLine (a0.x, a0.y, a1.x, a1.y, std::max (1.6f, d * 0.075f));
}

void BsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                                      juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }

    if (! ui::analog())
    {
        const float cxf = (float) x + (float) w * 0.5f;
        const float tp = (float) y, bt = (float) (y + h);
        juce::Rectangle<float> slotf (cxf - 2.5f, tp - 6.0f, 5.0f, bt - tp + 12.0f);
        g.setColour (ui::display);
        g.fillRoundedRectangle (slotf, 2.5f);
        g.setColour (ui::accentDeep);
        g.fillRoundedRectangle (cxf - 2.5f, sliderPos, 5.0f, bt + 6.0f - sliderPos, 2.5f);
        juce::Rectangle<float> capf (cxf - 19.0f, sliderPos - 10.0f, 38.0f, 20.0f);
        g.setColour (juce::Colours::black.withAlpha (ui::light() ? 0.14f : 0.35f));
        g.fillRoundedRectangle (capf.translated (0.0f, 1.5f), 6.0f);
        g.setColour (ui::capColour);
        g.fillRoundedRectangle (capf, 6.0f);
        if (ui::light()) { g.setColour (ui::ink.withAlpha (0.25f)); g.drawRoundedRectangle (capf, 6.0f, 1.0f); }
        g.setColour (ui::bg.withAlpha (0.9f).interpolatedWith (ui::ink, ui::light() ? 0.7f : 0.0f));
        g.fillRoundedRectangle (capf.getX() + 8.0f, capf.getCentreY() - 1.0f, capf.getWidth() - 16.0f, 2.2f, 1.0f);
        return;
    }
    const float cx = (float) (x + w) - 21.0f;
    const float top = (float) y, bottom = (float) (y + h);   // curso do centro da tampa

    // escala impressa (dB) à esquerda
    static const float marks[] = { 6.0f, 0.0f, -5.0f, -10.0f, -20.0f, -30.0f, -40.0f, -60.0f };
    g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    for (float m : marks)
    {
        const float yy = bottom - (float) s.valueToProportionOfLength ((double) m) * (bottom - top);
        g.setColour (ui::ink.withAlpha (m == 0.0f ? 0.95f : 0.65f));
        g.drawLine (cx - 27.0f, yy, cx - (m == 0.0f ? 20.0f : 23.0f), yy, m == 0.0f ? 1.4f : 1.0f);
        g.drawText (m <= -60.0f ? juce::String (juce::CharPointer_UTF8 ("-\xE2\x88\x9E")) : (m > 0 ? "+" : "") + juce::String ((int) m),
                    juce::Rectangle<float> (cx - 55.0f, yy - 6.0f, 27.0f, 12.0f), juce::Justification::centredRight, false);
    }

    // trilho
    juce::Rectangle<float> slot (cx - 3.0f, top - 8.0f, 6.0f, bottom - top + 16.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (slot, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine (slot.getX() + 0.5f, slot.getY() + 2.0f, slot.getX() + 0.5f, slot.getBottom() - 2.0f, 1.0f);
    g.setColour (ui::accent.withAlpha (0.8f));
    g.fillRoundedRectangle (cx - 1.0f, sliderPos, 2.0f, bottom + 8.0f - sliderPos, 1.0f);

    // tampa (cap) do fader: longa, off-white, com linha central
    juce::Rectangle<float> cap (cx - 17.0f, sliderPos - 23.0f, 34.0f, 46.0f);
    drawCap (g, cap, juce::Colour (0xffe6e0cf), 3.0f);
    g.setColour (ui::accent);
    g.fillRoundedRectangle (cap.getX() + 3.0f, cap.getY() + 5.0f, cap.getWidth() - 6.0f, 4.0f, 1.5f);
    g.setColour (ui::ink.withAlpha (0.28f));
    for (float dy : { -9.0f, -5.0f, 5.0f, 9.0f })
        g.drawLine (cap.getX() + 4.0f, cap.getCentreY() + 3.0f + dy, cap.getRight() - 4.0f, cap.getCentreY() + 3.0f + dy, 1.0f);
    g.setColour (ui::ink);
    g.fillRect (cap.getX() + 3.0f, cap.getCentreY() + 2.0f, cap.getWidth() - 6.0f, 2.2f);
}

// =================================================================================================
void DragHandle::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    if (! ui::analog())
    {
        g.setColour (active ? colour.withAlpha (ui::light() ? 0.14f : 0.2f) : ui::panel2);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (active ? colour.withAlpha (0.85f) : ui::textDim.withAlpha (0.3f));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
        g.setColour ((active ? colour : ui::textDim).withAlpha (0.9f));
        const float cy2 = b.getCentreY();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
                g.fillEllipse (b.getX() + 9.0f + (float) i * 4.0f, cy2 - 3.0f + (float) j * 5.0f, 2.0f, 2.0f);
        g.setColour (active ? ui::textMain : ui::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText (text, getLocalBounds().withTrimmedLeft (28), juce::Justification::centredLeft, true);
        return;
    }
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (ui::panel2.brighter (0.2f), 0.0f, b.getY(), ui::panel2.darker (0.10f), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (ui::ink.withAlpha (active ? 0.7f : 0.35f));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    // LED na lateral: acende com a cor da peça quando há arquivo para arrastar
    juce::Rectangle<float> led (b.getX() + 3.0f, b.getY() + 3.0f, 4.0f, b.getHeight() - 6.0f);
    g.setColour (active ? colour : ui::ink.withAlpha (0.2f));
    g.fillRoundedRectangle (led, 1.5f);

    // "pegador" de pontinhos
    g.setColour (ui::ink.withAlpha (active ? 0.55f : 0.25f));
    const float cy = b.getCentreY();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (b.getX() + 12.0f + (float) i * 4.0f, cy - 3.0f + (float) j * 5.0f, 2.0f, 2.0f);

    g.setColour (active ? ui::ink : ui::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    g.drawText (text, getLocalBounds().withTrimmedLeft (30), juce::Justification::centredLeft, true);
}

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! active || dragging || e.getDistanceFromDragStart() < 6 || ! getFiles) return;
    const auto files = getFiles();
    if (files.isEmpty()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this, [this] { dragging = false; });
}

// =================================================================================================
double WaveformView::xToSeconds (float x) const
{
    const double dur = proc.getDurationSeconds();
    return juce::jlimit (0.0, dur, (double) x / (double) std::max (1, getWidth()) * dur);
}

void WaveformView::mouseDown (const juce::MouseEvent& e) { downX = e.x; selecting = false; }

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (proc.getSource() == nullptr) return;
    if (std::abs (e.x - downX) > 3) selecting = true;
    if (selecting) proc.setSelection (xToSeconds ((float) downX), xToSeconds ((float) e.x));
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (! selecting && proc.getSource() != nullptr) proc.seekSeconds (xToSeconds ((float) e.x));
    selecting = false;
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (ui::display);
    g.fillRoundedRectangle (b, 4.0f);

    auto src = proc.getSource();
    if (src == nullptr)
    {
        g.setColour (ui::displayDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Clique em \"Carregar bateria...\" ou arraste um arquivo de áudio para cá"_u8, getLocalBounds(), juce::Justification::centred);
        return;
    }

    const double dur = src->original->seconds();
    const float w = b.getWidth(), h = b.getHeight();
    auto secToX = [&] (double s) { return (float) (s / dur * (double) w); };

    // seleção
    if (proc.hasSelection())
    {
        const float x0 = secToX (proc.getSelectionStart()), x1 = secToX (proc.getSelectionEnd());
        g.setColour (ui::led.withAlpha (0.28f));
        g.fillRect (x0, 0.0f, x1 - x0, h);
        g.setColour (ui::led.withAlpha (0.9f));
        g.drawVerticalLine ((int) x0, 0.0f, h);
        g.drawVerticalLine ((int) x1, 0.0f, h);
    }

    // grade de batidas
    if (getBeatPeriod)
    {
        const double per = getBeatPeriod();
        if (per > 0.05)
        {
            const double first = getFirstBeat ? getFirstBeat() : 0.0;
            g.setColour (ui::tick.withAlpha (0.16f));
            for (double t = std::fmod (first, per); t < dur; t += per)
                g.drawVerticalLine ((int) secToX (t), 0.0f, h);
        }
    }

    // forma de onda
    const auto& pk = src->peaks;
    const int nb = (int) pk.size() / 2;
    float maxPk = 0.05f;
    for (float v : pk) maxPk = std::max (maxPk, std::abs (v));
    const float mid = h * 0.5f, scale = mid * 0.9f / maxPk;
    g.setColour (ui::led.withAlpha (0.9f));
    const int wi = std::max (1, (int) w);
    for (int x = 0; x < wi; ++x)
    {
        const int i0 = x * nb / wi, i1 = std::max (i0 + 1, (x + 1) * nb / wi);
        float mn = 0.0f, mx = 0.0f;
        for (int i = i0; i < i1 && i < nb; ++i) { mn = std::min (mn, pk[(size_t) i * 2]); mx = std::max (mx, pk[(size_t) i * 2 + 1]); }
        g.drawVerticalLine (x, mid - mx * scale - 0.5f, mid - mn * scale + 0.5f);
    }

    // régua de tempo
    static const double steps[] = { 1, 2, 5, 10, 15, 30, 60, 120 };
    double step = steps[7];
    for (double s : steps) if (secToX (s) >= 80.0f) { step = s; break; }
    g.setFont (juce::FontOptions (11.0f));
    for (double t = 0.0; t < dur; t += step)
    {
        const float x = secToX (t);
        g.setColour (ui::displayDim.withAlpha (0.8f));
        g.drawVerticalLine ((int) x, 0.0f, 6.0f);
        g.drawText (juce::String ((int) t / 60) + ":" + juce::String ((int) t % 60).paddedLeft ('0', 2),
                    juce::Rectangle<float> (x + 3.0f, 0.0f, 50.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    // cabeça de reprodução
    const float px = secToX (proc.getPositionSeconds());
    g.setColour (ui::playhead);
    g.drawVerticalLine ((int) px, 0.0f, h);
    juce::Path tri;
    tri.addTriangle (px - 5.0f, 0.0f, px + 5.0f, 0.0f, px, 7.0f);
    g.fillPath (tri);
}

// =================================================================================================
namespace
{
void setupKnob (juce::Slider& k, juce::Colour cap, const juce::String& suffix, const juce::String& tip)
{
    k.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.setColour (juce::Slider::rotarySliderFillColourId, cap);
    k.setTextValueSuffix (suffix);
    k.setPopupDisplayEnabled (true, true, nullptr);
    k.setTooltip (tip);
    k.setMouseDragSensitivity (140);
}
}

StemStrip::StemStrip (BateriaSepProcessor& p, int stemIndex) : proc (p), index (stemIndex)
{
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fader);
    faderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::gainId (index), fader);

    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    addAndMakeVisible (dbLabel);

    if (index < kNumStems)
    {
        muteBtn.setClickingTogglesState (true);
        soloBtn.setClickingTogglesState (true);
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);
        muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, BateriaSepProcessor::muteId (index), muteBtn);
        soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, BateriaSepProcessor::soloId (index), soloBtn);

        drag.text = "Arrastar p/ DAW";
        drag.colour = ui::stemColours[index];
        drag.getFiles = [this]
        {
            juce::StringArray f;
            const auto all = proc.getStemPaths();   // já considera "Exportar com triggers"
            if (all.size() == kNumStems) f.add (all[index]);
            return f;
        };
        addAndMakeVisible (drag);

        // --- detecção: quando cada golpe dispara ---
        const auto orange = ui::accent, dark = juce::Colour (0xff2b2723);
        setupKnob (kThresh, orange, " dB", "THRESH (threshold): nível mínimo para um golpe contar.\nGire para a direita se disparar em sons fracos ou vazamento; para a esquerda para pegar golpes fracos (ghost notes). Duplo clique volta ao padrão.");
        setupKnob (kAttack, orange, " dB", "ATTACK: quão brusco o ataque precisa ser.\nGire para a direita para ignorar subidas suaves (ressonância, cauda, vazamento); para a esquerda para aceitar ataques mais suaves.");
        setupKnob (kHold, dark, " ms", "HOLD: intervalo mínimo entre dois golpes.\nGire para a direita se um golpe dispara duas vezes (duplo disparo); para a esquerda para permitir notas muito rápidas (rulos).");
        setupKnob (kReject, dark, " %", "REJECT (rejeição de cauda): descarta golpes fracos logo depois de um golpe forte.\nGire para a direita se disparar na ressonância/cauda da própria peça; 0 desliga.");
        for (auto* k : { &kThresh, &kAttack, &kHold, &kReject })
        {
            addAndMakeVisible (k);
        }
        kThresh.setDoubleClickReturnValue (true, 0.0);
        kAttack.setDoubleClickReturnValue (true, 0.0);
        kHold.setDoubleClickReturnValue (true, (double) bateriasep::defaultHoldMs (index));
        kReject.setDoubleClickReturnValue (true, (double) std::round (bateriasep::defaultRejectRatio (index) * 100.0f));
        threshAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::detThreshId (index), kThresh);
        attackAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::detAttackId (index), kAttack);
        holdAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::detHoldId (index), kHold);
        rejectAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::detRejectId (index), kReject);

        // --- trigger ---
        trigMode.addItemList ({ "Original", "Somar sample", "Substituir" }, 1);
        trigMode.setTooltip ("Original: só o som gravado.\nSomar: o sample toca junto com o som gravado.\nSubstituir: só o sample toca (o som gravado é silenciado).");
        addAndMakeVisible (trigMode);
        trigModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, BateriaSepProcessor::trigModeId (index), trigMode);

        trigSamplesBtn.setTooltip ("Escolher o kit padrão ou importar seus próprios samples");
        trigSamplesBtn.onClick = [this] { showSamplesMenu(); };
        addAndMakeVisible (trigSamplesBtn);

        setupKnob (trigLevel, juce::Colour (0xffe6e0cf), " dB", "LEVEL: volume do sample (a intensidade de cada golpe continua sendo respeitada).");
        trigLevel.setDoubleClickReturnValue (true, 0.0);
        addAndMakeVisible (trigLevel);
        trigLevelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, BateriaSepProcessor::trigLevelId (index), trigLevel);
    }
    else
    {
        trigExportBtn.setButtonText ("Exportar com triggers");
        trigExportBtn.setTooltip ("Ligado: \"Exportar\" e \"Arrastar\" usam o som dos triggers nas peças em Somar/Substituir");
        addAndMakeVisible (trigExportBtn);
        trigExportAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "trigExport", trigExportBtn);
    }
    applySkin();
}

void StemStrip::applySkin()
{
    const bool an = ui::analog();
    dbLabel.setColour (juce::Label::textColourId, an ? ui::displayText : ui::textDim);
    dbLabel.setColour (juce::Label::backgroundColourId, an ? ui::display : juce::Colours::transparentBlack);
    dbLabel.setFont (juce::Font (juce::FontOptions (an ? 12.0f : 11.5f, an ? juce::Font::bold : juce::Font::plain)));
    if (index < kNumStems)
    {
        muteBtn.setColour (juce::TextButton::buttonOnColourId, ui::red);
        muteBtn.setColour (juce::TextButton::textColourOnId, ui::light() ? juce::Colours::white : (an ? juce::Colours::white : ui::onAccent));
        soloBtn.setColour (juce::TextButton::buttonOnColourId, an ? juce::Colour (0xfff2b53a) : ui::amber);
        soloBtn.setColour (juce::TextButton::textColourOnId, an || ! ui::light() ? ui::onAccent : juce::Colours::white);
        drag.colour = ui::stemColours[index];
        const auto orange = ui::accent, dark = an ? juce::Colour (0xff2b2723) : (ui::light() ? juce::Colour (0xff59607a) : juce::Colour (0xff59607a));
        kThresh.setColour (juce::Slider::rotarySliderFillColourId, orange);
        kAttack.setColour (juce::Slider::rotarySliderFillColourId, orange);
        kHold.setColour (juce::Slider::rotarySliderFillColourId, dark);
        kReject.setColour (juce::Slider::rotarySliderFillColourId, dark);
        trigLevel.setColour (juce::Slider::rotarySliderFillColourId, ui::capColour);
    }
    repaint();
}

void StemStrip::showSamplesMenu()
{
    juce::PopupMenu m;
    const bool custom = shownSourceLabel.isNotEmpty() && ! shownSourceLabel.startsWith ("Kit");
    m.addItem (1, "Usar o kit padrão (Big Rusty Drums)"_u8, true, ! custom);
    m.addItem (2, "Importar meus samples..."_u8);
    m.addSeparator();
    m.addItem (3, "Dica: 1 arquivo = 1 intensidade. Com vários, use nomes em ordem (do suave ao forte)."_u8, false);
    juce::Component::SafePointer<StemStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trigSamplesBtn), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)
        {
            juce::Thread::launch ([safe]
            {
                if (safe == nullptr) return;
                auto& pr = safe->proc;
                pr.useDefaultTriggerKit (safe->index);
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onMessage) safe->onMessage ("Kit padrão restaurado."_u8); });
            });
        }
        else if (r == 2) safe->importSamples();
    });
}

void StemStrip::importSamples()
{
    sampleChooser = std::make_unique<juce::FileChooser> ("Escolha os samples (um ou vários)"_u8,
                                                         juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                         "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    juce::Component::SafePointer<StemStrip> safe (this);
    sampleChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
                                [safe] (const juce::FileChooser& fc)
                                {
                                    juce::StringArray files;
                                    for (auto& f : fc.getResults()) files.add (f.getFullPathName());
                                    if (safe == nullptr || files.isEmpty()) return;
                                    juce::Thread::launch ([safe, files]
                                    {
                                        if (safe == nullptr) return;
                                        juce::String msg;
                                        safe->proc.setTriggerSamples (safe->index, files, msg);
                                        juce::MessageManager::callAsync ([safe, msg] { if (safe != nullptr && safe->onMessage) safe->onMessage (msg); });
                                    });
                                });
}

void StemStrip::refresh (const std::shared_ptr<const StemData>& stems, bool hasFiles,
                         const std::shared_ptr<const std::array<std::vector<MidiHit>, 6>>& hits)
{
    if (stems != shown) { shown = stems; repaint(); }
    if (index < kNumStems)
    {
        drag.setActive (hasFiles);
        trigMode.setEnabled (stems != nullptr);
        trigSamplesBtn.setEnabled (stems != nullptr);
        for (auto* k : { &kThresh, &kAttack, &kHold, &kReject, &trigLevel }) k->setEnabled (stems != nullptr);
        const auto label = proc.getTriggerSourceLabel (index);
        if (label != shownSourceLabel)
        {
            shownSourceLabel = label;
            trigSamplesBtn.setButtonText (label.startsWith ("Kit") ? "Kit padrão"_u8 : label);
        }
        if (hits != shownHits)
        {
            shownHits = hits;
            hitCount = hits != nullptr ? (int) (*hits)[(size_t) index].size() : -1;
            repaint();
        }
    }
    else trigExportBtn.setEnabled (stems != nullptr);

    const float v = proc.getMeter (index);
    if (std::abs (v - meterShown) > 0.002f) { meterShown = v; repaint (meterArea.expanded (2)); }

    const float db = (float) fader.getValue();
    const auto t = dbText (db);
    if (dbLabel.getText() != t) dbLabel.setText (t, juce::dontSendNotification);
}

void StemStrip::resized()
{
    auto r = getLocalBounds().reduced (7, 7);
    r.removeFromTop (22);                       // placa com o nome
    if (index < kNumStems)
    {
        r.removeFromTop (4);
        waveArea = r.removeFromTop (38);
        r.removeFromTop (4);
        drag.setBounds (r.removeFromTop (22));
        r.removeFromTop (5);

        // detecção: 4 knobs
        detArea = r.removeFromTop (12);
        auto kr = r.removeFromTop (46);
        const int kw = kr.getWidth() / 4;
        juce::Slider* ks[4] = { &kThresh, &kAttack, &kHold, &kReject };
        for (int i = 0; i < 4; ++i)
        {
            auto cell = kr.removeFromLeft (kw);
            knobLabelArea[i] = cell.removeFromBottom (11);
            ks[i]->setBounds (cell.withSizeKeepingCentre (std::min (32, cell.getWidth()), std::min (32, cell.getHeight())));
        }
        r.removeFromTop (3);

        // trigger
        trigArea = r.removeFromTop (12);
        trigMode.setBounds (r.removeFromTop (22));
        r.removeFromTop (3);
        auto tr = r.removeFromTop (46);
        auto cell = tr.removeFromRight (kw + 4);
        knobLabelArea[4] = cell.removeFromBottom (11);
        trigLevel.setBounds (cell.withSizeKeepingCentre (32, std::min (32, cell.getHeight())));
        trigSamplesBtn.setBounds (tr.withSizeKeepingCentre (tr.getWidth() - 2, 22));
        r.removeFromTop (3);

        auto ms = r.removeFromBottom (28);
        muteBtn.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
        soloBtn.setBounds (ms.reduced (2, 0));
        r.removeFromBottom (4);
    }
    else
    {
        r.removeFromTop (4 + 38 + 4 + 22 + 5);
        trigExportBtn.setBounds (r.removeFromTop (12 + 46).withTrimmedTop (6).withHeight (32));
        r.removeFromTop (3);
        r.removeFromTop (12 + 22 + 3 + 46);
        r.removeFromBottom (32);
    }
    dbLabel.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);

    auto faderArea = r.removeFromLeft (r.getWidth() * 70 / 100);
    fader.setBounds (faderArea);
    meterArea = r.withSizeKeepingCentre (11, r.getHeight() - 10);
}

void StemStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool an = ui::analog();
    const auto colour = index < kNumStems ? ui::stemColours[index] : ui::accent;
    if (an)
    {
        g.setGradientFill (juce::ColourGradient (ui::panel2.brighter (0.15f), 0.0f, b.getY(), ui::panel2.darker (0.04f), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (ui::ink.withAlpha (0.5f));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);
    }
    else
    {
        g.setColour (ui::panel2);
        g.fillRoundedRectangle (b, 10.0f);
        if (ui::light()) { g.setColour (ui::ink.withAlpha (0.10f)); g.drawRoundedRectangle (b, 10.0f, 1.0f); }
        g.saveState();
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, getWidth(), 5));
        g.setColour (colour);
        g.fillRoundedRectangle (b, 10.0f);
        g.restoreState();
    }

    juce::Rectangle<float> plate (7.0f, 7.0f, b.getWidth() - 13.0f, 22.0f);
    const auto nameText = index < kNumStems ? juce::String (stemName (index)) : juce::String ("Master");
    if (an)
    {
        // placa do nome (preta, como a fita de nome dos canais SSL) com a cor da peça embaixo
        g.setColour (ui::display);
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (colour);
        g.fillRect (plate.getX() + 1.0f, plate.getBottom() - 3.0f, plate.getWidth() - 2.0f, 3.0f);
        g.setColour (ui::displayText);
        g.setFont (bold (14.0f));
        g.drawText (nameText.toUpperCase(), plate.reduced (7.0f, 0.0f).withTrimmedBottom (3.0f), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (ui::textMain);
        g.setFont (bold (14.5f));
        g.drawText (nameText, plate.reduced (4.0f, 0.0f).withTrimmedTop (2.0f), juce::Justification::centredLeft);
    }
    if (index < kNumStems && hitCount >= 0)
    {
        g.setColour (an ? ui::led : ui::textDim);
        g.setFont (bold (12.0f));
        g.drawText (juce::String (hitCount), plate.reduced (7.0f, 0.0f).withTrimmedBottom (an ? 3.0f : 0.0f).withTrimmedTop (an ? 0.0f : 2.0f), juce::Justification::centredRight);
    }

    auto heading = [&] (juce::Rectangle<int> a, const juce::String& t)
    {
        g.setColour (ui::ink);
        g.setFont (bold (9.5f));
        const int tw = (int) bold (9.5f).getStringWidthFloat (t) + 6;
        g.drawText (t, a.withWidth (tw), juce::Justification::centredLeft, false);
        g.setColour (ui::ink.withAlpha (0.45f));
        g.drawHorizontalLine (a.getCentreY(), (float) (a.getX() + tw), (float) a.getRight());
    };

    if (index < kNumStems)
    {
        // minitela com a forma de onda da peça e uma marca em cada golpe detectado
        g.setColour (ui::display);
        g.fillRoundedRectangle (waveArea.toFloat(), 3.0f);
        if (shown != nullptr && (size_t) index < shown->peaks.size())
        {
            const auto& pk = shown->peaks[(size_t) index];
            const int nb = (int) pk.size() / 2;
            float maxPk = 0.02f;
            for (float v : pk) maxPk = std::max (maxPk, std::abs (v));
            const float mid = waveArea.getCentreY(), sc = waveArea.getHeight() * 0.45f / maxPk;
            g.setColour ((an ? colour.brighter (0.35f) : colour).withAlpha (0.95f));
            for (int x = 0; x < waveArea.getWidth(); ++x)
            {
                const int i = x * nb / waveArea.getWidth();
                g.drawVerticalLine (waveArea.getX() + x, mid - pk[(size_t) i * 2 + 1] * sc - 0.5f, mid - pk[(size_t) i * 2] * sc + 0.5f);
            }
            if (shownHits != nullptr && shown->stems.size() > (size_t) index && shown->stems[(size_t) index]->seconds() > 0.0)
            {
                const double dur = shown->stems[(size_t) index]->seconds();
                g.setColour (ui::tick.withAlpha (0.85f));
                for (auto& h : (*shownHits)[(size_t) index])
                {
                    const int x = waveArea.getX() + (int) (h.time / dur * (double) waveArea.getWidth());
                    g.drawVerticalLine (x, (float) waveArea.getY() + 1.0f, (float) waveArea.getY() + 7.0f);
                }
            }
        }
        else
        {
            g.setColour (ui::displayDim.withAlpha (0.5f));
            g.drawHorizontalLine (waveArea.getCentreY(), (float) waveArea.getX() + 4.0f, (float) waveArea.getRight() - 4.0f);
        }

        heading (detArea, juce::String ("DETECÇÃO"_u8));
        static const char* names[4] = { "THRESH", "ATTACK", "HOLD", "REJECT" };
        g.setColour (ui::ink);
        g.setFont (bold (8.2f));
        for (int i = 0; i < 4; ++i) g.drawText (names[i], knobLabelArea[i].expanded (5, 0), juce::Justification::centred, false);
        heading (trigArea, "TRIGGER");
        g.setColour (ui::ink);
        g.drawText ("LEVEL", knobLabelArea[4], juce::Justification::centred, false);
    }
    else
    {
        // espaço correspondente (mantém os faders alinhados): um logotipinho serigrafado
        g.setColour (ui::ink.withAlpha (0.55f));
        g.setFont (bold (9.5f));
        g.drawText ("SAÍDA GERAL"_u8, getLocalBounds().withTop (36).withHeight (60), juce::Justification::centred, false);
    }

    if (! an)
    {
        // medidor em barra
        const auto mr = meterArea.toFloat();
        g.setColour (ui::display);
        g.fillRoundedRectangle (mr, 3.0f);
        const float lv = dbToPos (juce::Decibels::gainToDecibels (meterShown, -60.0f));
        const float fh = mr.getHeight() * lv;
        g.setColour (meterShown > 1.0f ? ui::red : colour);
        g.fillRoundedRectangle (mr.getX(), mr.getBottom() - fh, mr.getWidth(), fh, 3.0f);
        return;
    }

    // medidor de LEDs
    g.setColour (ui::display);
    g.fillRoundedRectangle (meterArea.toFloat().expanded (2.0f, 2.0f), 2.5f);
    const int nseg = 24;
    const float segH = (float) meterArea.getHeight() / (float) nseg;
    const float level = dbToPos (juce::Decibels::gainToDecibels (meterShown, -60.0f));
    for (int i = 0; i < nseg; ++i)
    {
        const float lo = (float) i / (float) nseg;
        const bool lit = level > lo + 0.5f / (float) nseg;
        const float db = lo * 66.0f - 60.0f;
        const juce::Colour on = db > -0.5f ? juce::Colour (0xffff4a3d) : (db > -18.0f ? ui::led : juce::Colour (0xff7ccf5a));
        g.setColour (lit ? on : juce::Colour (0xff2c2822));
        g.fillRect ((float) meterArea.getX() + 1.0f, (float) meterArea.getBottom() - (float) (i + 1) * segH + 0.8f, (float) meterArea.getWidth() - 2.0f, segH - 1.4f);
    }
}

// =================================================================================================
BateriaSepEditor::BateriaSepEditor (BateriaSepProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), wave (p)
{
    ui::setSkin (BateriaSepProcessor::loadSavedSkin());
    laf.updateColours();
    setLookAndFeel (&laf);
    loopBtn.setButtonText ("Repetir seleção"_u8);
    origBtn.setButtonText ("Ouvir original");
    gridBtn.setButtonText ("Grade de batidas");
    halfBtn.setButtonText ("÷2"_u8);
    doubleBtn.setButtonText ("×2"_u8);
    clearSelBtn.setButtonText ("Limpar seleção"_u8);

    for (auto* b : { &loadBtn, &cancelBtn, &modelBtn, &playBtn, &pauseBtn, &stopBtn, &halfBtn, &doubleBtn, &clearSelBtn, &exportBtn, &openFolderBtn })
        addAndMakeVisible (b);
    for (auto* b : { &loopBtn, &origBtn, &gridBtn })
        addAndMakeVisible (b);
    skinBox.addItemList ({ juce::String ("Visual: ") + ui::skinName (0), juce::String ("Visual: ") + ui::skinName (1), juce::String ("Visual: ") + ui::skinName (2) }, 1);
    skinBox.setSelectedItemIndex (ui::skin, juce::dontSendNotification);
    skinBox.setTooltip ("Escolha a aparência do plugin. Vale para todos os projetos.");
    skinBox.onChange = [this] { setSkin (skinBox.getSelectedItemIndex()); };
    addAndMakeVisible (skinBox);
    for (auto* l : { &timeLabel, &infoLabel, &bpmLabel, &statusLabel, &hintLabel })
        addAndMakeVisible (l);
    addAndMakeVisible (qualityBox);
    addAndMakeVisible (formatBox);
    addAndMakeVisible (progressBar);
    addAndMakeVisible (wave);
    addAndMakeVisible (dragAll);
    for (auto* b : { &exportMidiBtn }) addAndMakeVisible (b);
    addAndMakeVisible (midiSensSlider);
    addAndMakeVisible (midiSensLabel);
    addAndMakeVisible (midiInfoLabel);
    addAndMakeVisible (dragMidi);
    addAndMakeVisible (dragMidiOne);

    for (int i = 0; i < 7; ++i)
    {
        strips.add (new StemStrip (proc, i));
        strips.getLast()->onMessage = [this] (const juce::String& t) { flash (t); };
        addAndMakeVisible (strips.getLast());
    }

    qualityBox.addItemList ({ "Qualidade: Rápida"_u8, "Qualidade: Padrão"_u8 }, 1);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "quality", qualityBox);

    formatBox.addItemList ({ "WAV 32 bits float (sem alterar)", "WAV 24 bits (ganho de segurança)"_u8 }, 1);
    formatBox.setSelectedItemIndex (0, juce::dontSendNotification);

    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "loop", loopBtn);
    origAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "orig", origBtn);
    gridBtn.setToggleState (true, juce::dontSendNotification);

    timeLabel.setFont (juce::Font (juce::FontOptions (21.0f, juce::Font::bold)));
    timeLabel.setJustificationType (juce::Justification::centred);
    infoLabel.setFont (juce::FontOptions (13.0f));
    bpmLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setFont (juce::FontOptions (13.0f));
    hintLabel.setFont (juce::FontOptions (12.0f));

    hintLabel.setText ("Clique na onda para ir a um ponto  -  arraste para selecionar um trecho  -  duplo clique limpa a seleção"_u8,
                       juce::dontSendNotification);
    progressBar.setPercentageDisplay (false);

    dragAll.text = "Arrastar todos os stems para a DAW";
    dragAll.getFiles = [this] { return proc.getStemPaths(); };

    // --- MIDI ---
    midiSensLabel.setText ("SENSIBILIDADE GERAL"_u8, juce::dontSendNotification);
    midiSensLabel.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    midiInfoLabel.setFont (juce::FontOptions (11.5f));
    midiInfoLabel.setMinimumHorizontalScale (1.0f);
    midiInfoLabel.setJustificationType (juce::Justification::centredLeft);
    midiSensSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    midiSensSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    midiSensSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    midiSensSlider.setPopupDisplayEnabled (true, true, nullptr);
    midiSensSlider.setMouseDragSensitivity (140);
    midiSensSlider.setTooltip ("Sensibilidade geral da detecção (vale para todas as peças). Mais à direita = mais notas (inclusive as bem fracas); mais à esquerda = só os golpes fortes. Para acertar uma peça só, use os knobs dela.");
    midiSensAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "midiSens", midiSensSlider);
    dragMidi.text = "Arrastar stems MIDI";

    dragMidi.getFiles = [this] { return proc.getMidiPaths(); };
    dragMidiOne.text = "Arrastar MIDI único"_u8;

    dragMidiOne.getFiles = [this]
    {
        juce::StringArray a;
        const auto p = proc.getMidiCombinedPath();
        if (p.isNotEmpty()) a.add (p);
        return a;
    };
    exportMidiBtn.onClick = [this] { chooseMidiFolder(); };

    wave.getBeatPeriod = [this]
    {
        if (! gridBtn.getToggleState() || shownStems == nullptr || ! shownStems->bpm.valid) return 0.0;
        return 60.0 / (shownStems->bpm.bpm * bpmFactor);
    };
    wave.getFirstBeat = [this] { return shownStems != nullptr ? shownStems->bpm.firstBeatSec : 0.0; };

    loadBtn.onClick = [this] { chooseAudioFile(); };
    cancelBtn.onClick = [this] { proc.cancelLoad(); };
    modelBtn.onClick = [this] { chooseModelFile(); };
    playBtn.onClick = [this] { proc.play(); };
    pauseBtn.onClick = [this] { proc.pause(); };
    stopBtn.onClick = [this] { proc.stop(); };
    clearSelBtn.onClick = [this] { proc.clearSelection(); };
    halfBtn.onClick = [this] { bpmFactor = std::max (0.25, bpmFactor * 0.5); };
    doubleBtn.onClick = [this] { bpmFactor = std::min (4.0, bpmFactor * 2.0); };
    exportBtn.onClick = [this] { chooseExportFolder(); };
    openFolderBtn.onClick = [this]
    {
        if (auto sd = proc.getStems()) if (sd->folder.isDirectory()) sd->folder.revealToUser();
    };

    setSize (1120, 944);   // por último: resized() precisa que todos os componentes já existam
    startTimerHz (30);
    timerCallback();
}

BateriaSepEditor::~BateriaSepEditor() { setLookAndFeel (nullptr); }

void BateriaSepEditor::setSkin (int index, bool save)
{
    ui::setSkin (index);
    if (save) BateriaSepProcessor::saveSkinChoice (ui::skin);
    if (skinBox.getSelectedItemIndex() != ui::skin) skinBox.setSelectedItemIndex (ui::skin, juce::dontSendNotification);
    applySkin();
}

void BateriaSepEditor::applySkin()
{
    const bool an = ui::analog();
    laf.updateColours();
    timeLabel.setColour (juce::Label::backgroundColourId, an ? ui::display : juce::Colours::transparentBlack);
    timeLabel.setColour (juce::Label::textColourId, an ? ui::led : ui::textMain);
    timeLabel.setJustificationType (an ? juce::Justification::centred : juce::Justification::centredLeft);
    timeLabel.setFont (juce::Font (juce::FontOptions (an ? 21.0f : 19.0f, juce::Font::bold)));
    infoLabel.setColour (juce::Label::textColourId, an ? ui::displayText : ui::textDim);
    hintLabel.setColour (juce::Label::textColourId, an ? ui::displayDim : ui::textDim.withAlpha (0.85f));
    loadBtn.setColour (juce::TextButton::buttonColourId, ui::accent);
    loadBtn.setColour (juce::TextButton::textColourOffId, ui::onAccent);
    cancelBtn.setColour (juce::TextButton::buttonColourId, ui::panel2);
    midiSensLabel.setColour (juce::Label::textColourId, an ? ui::ink : ui::textDim);
    midiSensLabel.setText (an ? juce::String ("SENSIBILIDADE GERAL") : juce::String ("Sensibilidade geral"), juce::dontSendNotification);
    midiSensLabel.setFont (juce::Font (juce::FontOptions (an ? 10.0f : 12.0f, an ? juce::Font::bold : juce::Font::plain)));
    midiInfoLabel.setColour (juce::Label::textColourId, ui::textDim);
    midiSensSlider.setColour (juce::Slider::rotarySliderFillColourId, ui::accent);
    dragMidi.colour = an ? ui::accent : ui::amber;
    dragMidiOne.colour = an ? ui::accent : ui::amber;
    dragAll.colour = ui::accent;
    for (auto* st : strips) st->applySkin();
    resized();
    sendLookAndFeelChange();
    repaint();
}

void BateriaSepEditor::paint (juce::Graphics& g)
{
    if (! ui::analog())
    {
        g.fillAll (ui::bg);
        auto card = [&] (juce::Rectangle<int> r)
        {
            g.setColour (ui::panel);
            g.fillRoundedRectangle (r.toFloat(), 12.0f);
            if (ui::light()) { g.setColour (ui::ink.withAlpha (0.10f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 12.0f, 1.0f); }
        };
        card (displayArea.getUnion (controlArea));
        card (mixerArea);
        g.setColour (ui::textMain);
        g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
        g.drawText ("BateriaSep", logoArea.withTrimmedLeft (6).withWidth (170), juce::Justification::centredLeft);
        g.setColour (ui::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Separador de bateria com IA", logoArea.withTrimmedLeft (178), juce::Justification::centredLeft);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText ("STEMS", mixerArea.getX() + 14, mixerArea.getY() + 6, 100, 12, juce::Justification::centredLeft);
        return;
    }

    const auto W = (float) getWidth(), H = (float) getHeight();

    // chassi off-white
    g.setGradientFill (juce::ColourGradient (ui::bg.brighter (0.04f), 0.0f, 0.0f, ui::bg.darker (0.05f), 0.0f, H, false));
    g.fillAll();

    // laterais de madeira (como as "cheeks" de uma mesa de som)
    for (int side = 0; side < 2; ++side)
    {
        const float x = side == 0 ? 0.0f : W - kCheek;
        g.setGradientFill (juce::ColourGradient (ui::wood1, x, 0.0f, ui::wood2, x + kCheek, 0.0f, false));
        g.fillRect (x, 0.0f, kCheek, H);
        juce::Random rnd (side + 5);
        for (int i = 0; i < 26; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.05f + 0.10f * rnd.nextFloat()));
            const float gx = x + rnd.nextFloat() * kCheek;
            g.drawLine (gx, 0.0f, gx + (rnd.nextFloat() - 0.5f) * 4.0f, H, 1.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawVerticalLine ((int) (side == 0 ? x + 1.0f : x + kCheek - 2.0f), 0.0f, H);
    }
    g.setColour (ui::ink.withAlpha (0.7f));
    g.drawVerticalLine ((int) kCheek, 0.0f, H);
    g.drawVerticalLine ((int) (W - kCheek - 1.0f), 0.0f, H);

    // placa do logotipo
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (logoArea.toFloat().translated (0.0f, 1.5f), 4.0f);
    g.setGradientFill (juce::ColourGradient (ui::accent.brighter (0.15f), 0.0f, (float) logoArea.getY(), ui::accentDeep, 0.0f, (float) logoArea.getBottom(), false));
    g.fillRoundedRectangle (logoArea.toFloat(), 4.0f);
    g.setColour (ui::ink.withAlpha (0.8f));
    g.drawRoundedRectangle (logoArea.toFloat(), 4.0f, 1.2f);
    g.setColour (juce::Colour (0xfffbf6ea));
    g.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::bold)));
    g.drawText ("BATERIASEP", logoArea.withTrimmedLeft (14).withWidth (190), juce::Justification::centredLeft);
    g.setColour (ui::ink);
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText ("DRUM SEPARATOR", logoArea.withTrimmedLeft (196).withTrimmedBottom (13), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xfffbf6ea));
    g.drawText ("SERIES 9000", logoArea.withTrimmedLeft (196).withTrimmedTop (14), juce::Justification::centredLeft);

    // visor (meter bridge): moldura escura com a onda, status e progresso
    auto bezel = displayArea.toFloat();
    g.setColour (juce::Colour (0xff0a0908));
    g.fillRoundedRectangle (bezel, 6.0f);
    g.setColour (juce::Colour (0xff4a443a));
    g.drawRoundedRectangle (bezel.reduced (0.5f), 6.0f, 1.2f);
    g.setColour (juce::Colour (0xff1c1a17));
    g.fillRoundedRectangle (bezel.reduced (5.0f), 4.0f);

    // faixa de controles
    g.setColour (ui::panel);
    g.fillRoundedRectangle (controlArea.toFloat(), 5.0f);
    g.setColour (ui::ink.withAlpha (0.45f));
    g.drawRoundedRectangle (controlArea.toFloat().reduced (0.5f), 5.0f, 1.0f);

    // mesa de canais
    g.setColour (ui::panel);
    g.fillRoundedRectangle (mixerArea.toFloat(), 6.0f);
    g.setColour (ui::ink.withAlpha (0.6f));
    g.drawRoundedRectangle (mixerArea.toFloat().reduced (0.5f), 6.0f, 1.2f);
    g.setColour (ui::ink);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("CANAIS  /  CHANNELS", mixerArea.getX() + 22, mixerArea.getY() + 3, 200, 13, juce::Justification::centredLeft);
    g.setColour (ui::accent);
    g.fillRect ((float) mixerArea.getX() + 10.0f, (float) mixerArea.getY() + 8.0f, 8.0f, 3.0f);
    for (auto* r : { &mixerArea, &controlArea, &footerArea })
        for (int i = 0; i < 4; ++i)
            BsLookAndFeel::drawScrew (g, { (float) (i % 2 == 0 ? r->getX() + 7 : r->getRight() - 7), (float) (i < 2 ? r->getY() + 7 : r->getBottom() - 7) }, 3.0f);

    // base (apoio de braço) com as exportações
    g.setColour (ui::panel);
    g.fillRoundedRectangle (footerArea.toFloat(), 6.0f);
    g.setColour (ui::ink.withAlpha (0.6f));
    g.drawRoundedRectangle (footerArea.toFloat().reduced (0.5f), 6.0f, 1.2f);
    for (auto* r : { &footerArea })
        for (int i = 0; i < 4; ++i)
            BsLookAndFeel::drawScrew (g, { (float) (i % 2 == 0 ? r->getX() + 7 : r->getRight() - 7), (float) (i < 2 ? r->getY() + 7 : r->getBottom() - 7) }, 3.0f);
}

void BateriaSepEditor::resized()
{
    if (strips.size() < 7) return;
    auto r = getLocalBounds().withTrimmedLeft ((int) kCheek + 12).withTrimmedRight ((int) kCheek + 12).reduced (0, 12);

    auto top = r.removeFromTop (46);
    logoArea = top.removeFromLeft (330);
    modelBtn.setBounds (top.removeFromRight (140).reduced (0, 7));
    top.removeFromRight (8);
    qualityBox.setBounds (top.removeFromRight (150).reduced (0, 7));
    top.removeFromRight (8);
    skinBox.setBounds (top.removeFromRight (160).reduced (0, 7));
    top.removeFromRight (8);
    cancelBtn.setBounds (top.removeFromRight (90).reduced (0, 7));
    top.removeFromRight (8);
    loadBtn.setBounds (top.removeFromRight (150).reduced (0, 7));
    r.removeFromTop (8);

    // --- visor ---
    displayArea = r.removeFromTop (164);
    auto p = displayArea.reduced (12, 10);
    infoLabel.setBounds (p.removeFromTop (18));
    p.removeFromTop (3);
    wave.setBounds (p.removeFromTop (70));
    p.removeFromTop (3);
    hintLabel.setBounds (p.removeFromTop (15));
    p.removeFromTop (2);
    auto statusRow = p.removeFromTop (20);
    progressBar.setBounds (statusRow.removeFromRight (230).reduced (0, 2));
    statusRow.removeFromRight (10);
    statusLabel.setBounds (statusRow);
    r.removeFromTop (6);

    // --- faixa de controles (transporte, tempo, opções) ---
    controlArea = r.removeFromTop (70);
    auto c = controlArea.reduced (16, 8);
    auto tr = c.removeFromTop (30);
    playBtn.setBounds (tr.removeFromLeft (80));  tr.removeFromLeft (6);
    pauseBtn.setBounds (tr.removeFromLeft (80)); tr.removeFromLeft (6);
    stopBtn.setBounds (tr.removeFromLeft (80));  tr.removeFromLeft (14);
    timeLabel.setBounds (tr.removeFromLeft (170));
    bpmLabel.setBounds (tr.removeFromRight (380));
    doubleBtn.setBounds (tr.removeFromRight (40));  tr.removeFromRight (4);
    halfBtn.setBounds (tr.removeFromRight (40));
    c.removeFromTop (4);
    auto opts = c.removeFromTop (22);
    loopBtn.setBounds (opts.removeFromLeft (160));
    origBtn.setBounds (opts.removeFromLeft (160));
    gridBtn.setBounds (opts.removeFromLeft (170));
    clearSelBtn.setBounds (opts.removeFromRight (140));
    r.removeFromTop (8);

    // --- base: exportar stems / MIDI ---
    footerArea = r.removeFromBottom (112);
    auto f = footerArea.reduced (18, 8);
    auto footer = f.removeFromTop (44);
    exportBtn.setBounds (footer.removeFromLeft (210).reduced (0, 6));
    footer.removeFromLeft (8);
    formatBox.setBounds (footer.removeFromLeft (250).reduced (0, 6));
    openFolderBtn.setBounds (footer.removeFromRight (170).reduced (0, 6));
    footer.removeFromRight (8);
    dragAll.setBounds (footer.removeFromRight (300).reduced (0, 6));
    f.removeFromTop (2);
    auto footerMidi = f.removeFromTop (44);
    exportMidiBtn.setBounds (footerMidi.removeFromLeft (150).reduced (0, 6));
    footerMidi.removeFromLeft (16);
    midiSensSlider.setBounds (footerMidi.removeFromLeft (38).reduced (0, 3));
    footerMidi.removeFromLeft (4);
    midiSensLabel.setBounds (footerMidi.removeFromLeft (150).reduced (0, 10));
    footerMidi.removeFromLeft (10);
    dragMidiOne.setBounds (footerMidi.removeFromRight (172).reduced (0, 6));
    footerMidi.removeFromRight (8);
    dragMidi.setBounds (footerMidi.removeFromRight (172).reduced (0, 6));
    footerMidi.removeFromRight (10);
    midiInfoLabel.setBounds (footerMidi);
    r.removeFromBottom (8);

    // --- mesa ---
    mixerArea = r;
    auto m = mixerArea.reduced (14, 8);
    m.removeFromTop (12);
    const int gap = 8;
    const int w = (m.getWidth() - gap * 6) / 7;
    for (int i = 0; i < 7; ++i)
    {
        strips[i]->setBounds (m.removeFromLeft (w));
        m.removeFromLeft (gap);
    }
}

juce::String BateriaSepEditor::formatTime (double s) const
{
    const int m = (int) s / 60;
    const double sec = s - m * 60;
    return juce::String (m) + ":" + juce::String (sec, 1).paddedLeft ('0', 4);
}

void BateriaSepEditor::flash (const juce::String& text)
{
    flashText = text;
    flashUntil = juce::Time::getMillisecondCounterHiRes() + 9000.0;
}

void BateriaSepEditor::timerCallback()
{
    auto src = proc.getSource();
    auto sd = proc.getStems();
    const bool busy = proc.isBusy();
    const bool hasStems = sd != nullptr;
    const bool hasFiles = hasStems && sd->folder.isDirectory();

    if (src != shownSource)
    {
        if (src == nullptr || shownSource == nullptr || src->file != shownSource->file) bpmFactor = 1.0;
        shownSource = src;
        wave.repaint();
    }
    shownStems = sd;

    // informações do arquivo
    if (src != nullptr)
    {
        infoLabel.setText (src->file.getFileName() + "   -   " + formatTime (src->original->seconds()) + "   -   "
                           + juce::String (src->original->sampleRate / 1000.0, 1) + " kHz   -   "
                           + (src->fileChannels == 1 ? "mono" : "estéreo"_u8), juce::dontSendNotification);
    }
    else
    {
        infoLabel.setText ("Nenhum arquivo carregado", juce::dontSendNotification);
    }

    timeLabel.setText (formatTime (proc.getPositionSeconds()) + " / " + formatTime (proc.getDurationSeconds()), juce::dontSendNotification);

    // BPM
    if (hasStems && sd->bpm.valid)
    {
        const double v = sd->bpm.bpm * bpmFactor;
        bpmLabel.setText ("BPM provável: "_u8 + juce::String (v, 1) + "   (metade " + juce::String ((int) std::lround (v * 0.5)) + " / dobro " + juce::String ((int) std::lround (v * 2.0))
                          + ")   confiança "_u8 + juce::String ((int) (sd->bpm.confidence * 100)) + "%", juce::dontSendNotification);
        bpmLabel.setColour (juce::Label::textColourId, ui::analog() ? ui::accentDeep : (ui::light() ? ui::accent : ui::amber));
    }
    else
    {
        bpmLabel.setText (hasStems ? "BPM: não foi possível estimar"_u8 : "BPM: aparece depois da separação"_u8, juce::dontSendNotification);
        bpmLabel.setColour (juce::Label::textColourId, ui::textDim);
    }

    // status
    bool isError = false;
    auto status = proc.getStatus (&isError);
    if (juce::Time::getMillisecondCounterHiRes() < flashUntil) { status = flashText; isError = false; }
    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? (ui::analog() ? juce::Colour (0xffff7a6b) : ui::red)
                                                              : (ui::analog() ? ui::displayText.withAlpha (0.75f) : ui::textDim));
    progressValue = proc.getProgress();
    progressBar.setVisible (busy);
    cancelBtn.setVisible (busy);

    // habilita / desabilita
    loadBtn.setEnabled (! busy);
    for (auto* b : { &playBtn, &pauseBtn, &stopBtn, &clearSelBtn }) b->setEnabled (src != nullptr);
    origBtn.setEnabled (hasStems);
    exportBtn.setEnabled (hasStems);
    openFolderBtn.setEnabled (hasFiles);
    gridBtn.setEnabled (hasStems);
    halfBtn.setEnabled (hasStems);
    doubleBtn.setEnabled (hasStems);
    dragAll.setActive (hasFiles);

    // --- MIDI: informa o BPM exibido e regera os arquivos (com pequeno atraso) quando algo muda ---
    {
        const double bpmShown = (sd != nullptr && sd->bpm.valid) ? sd->bpm.bpm * bpmFactor : 0.0;
        proc.setMidiTempo (bpmShown);
        const uint64_t sig = proc.detectSignature();
        const void* key = sd.get();
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (key != midiKeyStems || sig != midiKeySig || std::abs (bpmShown - midiKeyBpm) > 1e-3)
        {
            midiKeyStems = key; midiKeySig = sig; midiKeyBpm = bpmShown;
            midiRefreshAt = nowMs + 350.0;
        }
        if (hasStems && midiRefreshAt > 0.0 && nowMs >= midiRefreshAt && ! midiBusy)
        {
            midiRefreshAt = 0.0;
            midiBusy = true;
            juce::Component::SafePointer<BateriaSepEditor> safe (this);
            juce::Thread::launch ([safe, this]
            {
                proc.refreshMidi();
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->midiBusy = false; });
            });
        }
        // triggers: monta o plano (notas + samples) quando alguma peça usa trigger e ele ainda não está pronto
        if (hasStems && ! midiBusy && midiRefreshAt == 0.0 && ! trigBusy && nowMs >= trigRetryAt
            && proc.anyTriggerOn() && ! proc.triggerPlanReady())
        {
            trigBusy = true;
            trigRetryAt = nowMs + 3000.0;
            juce::Component::SafePointer<BateriaSepEditor> safe (this);
            juce::Thread::launch ([safe, this]
            {
                proc.rebuildTriggerPlan();
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->trigBusy = false; });
            });
        }
        midiInfoLabel.setText (hasStems ? (midiBusy || midiRefreshAt > 0.0 ? juce::String ("Gerando MIDI...") : proc.getMidiSummary())
                                        : juce::String(), juce::dontSendNotification);
        exportMidiBtn.setEnabled (hasStems);
        midiSensSlider.setEnabled (hasStems);
        dragMidi.setActive (hasStems);
        dragMidiOne.setActive (hasStems);
    }
    playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification);
    playBtn.setColour (juce::TextButton::buttonColourId, proc.isPlaying() ? ui::accent : ui::panel2);
    playBtn.setColour (juce::TextButton::textColourOffId, proc.isPlaying() ? ui::onAccent : ui::textMain);

    const auto hitsNow = proc.getHits();
    for (auto* s : strips) s->refresh (sd, hasFiles, hitsNow);
    wave.repaint();
}

// -------------------------------------------------------------------------------------------------
void BateriaSepEditor::chooseAudioFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha o arquivo de bateria",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadFile (f);
                          });
}

void BateriaSepEditor::chooseModelFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Localize o arquivo drumsep_core.onnx", juce::File(), "*.onnx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                              {
                                  proc.setModelFile (f);
                                  flash ("Modelo definido: " + f.getFullPathName() + ". Agora clique em \"Carregar bateria...\".");
                              }
                          });
}

void BateriaSepEditor::chooseExportFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha a pasta onde gravar os stems",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder == juce::File()) return;

                              flash ("Gravando os arquivos...");
                              const int fmt = formatBox.getSelectedItemIndex();
                              juce::Component::SafePointer<BateriaSepEditor> safe (this);
                              juce::Thread::launch ([safe, folder, fmt, this]
                              {
                                  juce::String msg;
                                  proc.exportStems (folder, fmt, msg);
                                  juce::MessageManager::callAsync ([safe, msg] { if (safe != nullptr) safe->flash (msg); });
                              });
                          });
}

void BateriaSepEditor::chooseMidiFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Escolha a pasta onde gravar os arquivos MIDI",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder == juce::File()) return;
                              juce::String msg;
                              proc.exportMidi (folder, msg);
                              flash (msg);
                          });
}

bool BateriaSepEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && ! proc.isBusy();
}

void BateriaSepEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() == 1 && ! proc.isBusy()) proc.loadFile (juce::File (files[0]));
}
