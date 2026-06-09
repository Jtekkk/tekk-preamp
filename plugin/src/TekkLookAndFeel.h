#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ============================================================================
//  TekkLookAndFeel : the dark / cyberpunk skin.
//
//  Knobs are drawn as a dim full-sweep track with a bright value arc painted
//  twice -- once thick at low alpha (the neon "glow"), once thin at full alpha
//  on top. Each slider can be tinted per-instance through
//  rotarySliderFillColourId, so the hero controls (Drive, Bias) glow magenta
//  while the rest sit in cyan. No bitmaps: everything is vector so it scales.
// ============================================================================
class TekkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // --- palette ----------------------------------------------------------
    static constexpr juce::uint32 cBgDeep   = 0xff0a0b10;
    static constexpr juce::uint32 cBgPanel  = 0xff12141c;
    static constexpr juce::uint32 cBgKnob   = 0xff191d2a;
    static constexpr juce::uint32 cBgKnobHi = 0xff232a3d;
    static constexpr juce::uint32 cTrack    = 0xff2b3450;
    static constexpr juce::uint32 cCyan     = 0xff00e5ff;
    static constexpr juce::uint32 cMagenta  = 0xffff2a6d;
    static constexpr juce::uint32 cTextHi   = 0xffe8f0ff;
    static constexpr juce::uint32 cTextDim  = 0xff7c88ac;

    TekkLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (cBgDeep));
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (cCyan));
        setColour (juce::Slider::textBoxTextColourId,         juce::Colour (cTextHi));
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,                 juce::Colour (cTextDim));

        setColour (juce::ComboBox::backgroundColourId,        juce::Colour (cBgKnob));
        setColour (juce::ComboBox::textColourId,              juce::Colour (cTextHi));
        setColour (juce::ComboBox::outlineColourId,           juce::Colour (cTrack));
        setColour (juce::ComboBox::arrowColourId,             juce::Colour (cCyan));
        setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (cBgPanel));
        setColour (juce::PopupMenu::textColourId,             juce::Colour (cTextHi));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (cCyan).withAlpha (0.18f));
        setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colour (cCyan));
    }

    // --- rotary knob ------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle,
                           juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float angle  = startAngle + pos * (endAngle - startAngle);
        const auto accent  = s.findColour (juce::Slider::rotarySliderFillColourId);

        // knob body: subtle top-lit radial fill + rim
        const float bodyR = radius * 0.74f;
        juce::ColourGradient body (juce::Colour (cBgKnobHi), centre.x, centre.y - bodyR,
                                   juce::Colour (cBgKnob),   centre.x, centre.y + bodyR, false);
        g.setGradientFill (body);
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));
        g.setColour (juce::Colour (cTrack));
        g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre), 1.2f);

        const float arcR = radius * 0.90f;
        const float thick = radius * 0.13f;

        // dim full-sweep track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (cTrack));
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // value arc -- glow then core
        if (pos > 0.0001f)
        {
            juce::Path val;
            val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (accent.withAlpha (0.22f));
            g.strokePath (val, juce::PathStrokeType (thick * 2.1f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // pointer
        const float pinLen = bodyR * 0.82f;
        const juce::Point<float> tip (centre.x + pinLen * std::cos (angle - juce::MathConstants<float>::halfPi),
                                      centre.y + pinLen * std::sin (angle - juce::MathConstants<float>::halfPi));
        g.setColour (accent.withAlpha (0.30f));
        g.drawLine ({ centre, tip }, 4.2f);
        g.setColour (juce::Colour (cTextHi));
        g.drawLine ({ centre, tip }, 1.8f);
        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (centre));
    }

    // --- toggle (neon pill switch) ---------------------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool /*highlighted*/, bool /*down*/) override
    {
        auto r = b.getLocalBounds().toFloat();
        const float h = juce::jmin (18.0f, r.getHeight());
        auto pill = juce::Rectangle<float> (36.0f, h).withY (r.getCentreY() - h * 0.5f);
        const bool on = b.getToggleState();
        const auto accent = juce::Colour (cCyan);

        g.setColour (juce::Colour (cBgKnob));
        g.fillRoundedRectangle (pill, h * 0.5f);
        g.setColour (on ? accent : juce::Colour (cTrack));
        g.drawRoundedRectangle (pill, h * 0.5f, 1.2f);
        if (on)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (pill, h * 0.5f);
        }
        const float knobD = h - 5.0f;
        const float kx = on ? pill.getRight() - knobD - 2.5f : pill.getX() + 2.5f;
        g.setColour (on ? accent : juce::Colour (cTextDim));
        g.fillEllipse (kx, pill.getCentreY() - knobD * 0.5f, knobD, knobD);

        g.setColour (b.findColour (juce::ToggleButton::textColourId));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (b.getButtonText(), pill.getRight() + 8.0f, r.getY(),
                    r.getWidth() - pill.getRight() - 8.0f, r.getHeight(),
                    juce::Justification::centredLeft);
    }

    // --- generic button background (used by the segmented character switch) -
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& /*bg*/, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        // hero controls can opt into a different glow via an "accent" property
        const auto accent = juce::Colour (
            (juce::uint32) (int) b.getProperties().getWithDefault ("accent", (int) cCyan));

        g.setColour (juce::Colour (on ? cBgKnobHi : cBgPanel));
        g.fillRoundedRectangle (r, 4.0f);
        if (on || highlighted || down)
        {
            g.setColour (accent.withAlpha (on ? 0.18f : 0.08f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        g.setColour (on ? accent : juce::Colour (cTrack));
        g.drawRoundedRectangle (r, 4.0f, on ? 1.6f : 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (1.0f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        juce::Path tri;
        const float cx = width - 14.0f, cy = height * 0.5f;
        tri.addTriangle (cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (tri);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font (juce::FontOptions (12.5f));
    }

    static juce::Font titleFont (float h)
    {
        return juce::Font (juce::FontOptions (h).withStyle ("Bold"));
    }
};
