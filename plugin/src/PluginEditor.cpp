#include "PluginEditor.h"
#include <array>

using LF = TekkLookAndFeel;

static constexpr int kHeaderH = 72;   // shared by paint() + resized()
static constexpr int kFooterH = 52;

// ===========================================================================
//  LevelMeter
// ===========================================================================
float LevelMeter::normDb (float peak)
{
    const float dbv = juce::Decibels::gainToDecibels (peak, -60.0f);
    return juce::jlimit (0.0f, 1.0f, juce::jmap (dbv, -48.0f, 6.0f, 0.0f, 1.0f));
}

void LevelMeter::update()
{
    const float target = normDb (source.load (std::memory_order_relaxed));
    if (target >= level) level = target;                       // instant attack
    else                 level += (target - level) * 0.35f;    // eased release

    if (target >= peakHold) { peakHold = target; holdFrames = 18; }
    else if (--holdFrames <= 0) peakHold = juce::jmax (0.0f, peakHold - 0.015f);

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto full   = getLocalBounds().toFloat();
    auto capRow = full.removeFromBottom (16.0f);
    auto track  = full.reduced (2.0f);

    g.setColour (juce::Colour (LF::cBgKnob));
    g.fillRoundedRectangle (track, 3.0f);

    auto inner = track.reduced (2.0f);
    const float ih = inner.getHeight();

    if (level > 0.001f)
    {
        juce::Rectangle<float> bar = inner.withTop (inner.getBottom() - ih * level);
        juce::ColourGradient grad (juce::Colour (LF::cCyan),    0.0f, inner.getBottom(),
                                   juce::Colour (LF::cMagenta), 0.0f, inner.getY(), false);
        grad.addColour (0.82, juce::Colour (0xffffc400));      // amber shoulder near 0 dBFS
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, 2.0f);
    }

    if (peakHold > 0.001f)
    {
        const float y = inner.getBottom() - ih * peakHold;
        g.setColour (juce::Colour (LF::cTextHi));
        g.fillRect (inner.getX(), y - 0.75f, inner.getWidth(), 1.5f);
    }

    g.setColour (juce::Colour (LF::cTrack));
    g.drawRoundedRectangle (track, 3.0f, 1.0f);

    g.setColour (juce::Colour (LF::cTextDim));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (label, capRow, juce::Justification::centred);
}

// ===========================================================================
//  PreampEditor
// ===========================================================================
PreampEditor::PreampEditor (PreampProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    const auto cyan = juce::Colour (LF::cCyan);
    const auto mag  = juce::Colour (LF::cMagenta);

    setupKnob (sInTrim,  lInTrim,  "INPUT",    " dB", cyan);
    setupKnob (sDrive,   lDrive,   "DRIVE",    "",    mag);
    setupKnob (sBias,    lBias,    "BIAS",     "",    mag);
    setupKnob (sOutTrim, lOutTrim, "OUTPUT",   " dB", cyan);
    setupKnob (sInIron,  lInIron,  "IN IRON",  "",    cyan);
    setupKnob (sOutIron, lOutIron, "OUT IRON", "",    cyan);
    setupKnob (sHpf,     lHpf,     "HPF",      " Hz", cyan);

    aInTrim  = std::make_unique<SA> (proc.apvts, PID::inTrim,  sInTrim);
    aDrive   = std::make_unique<SA> (proc.apvts, PID::drive,   sDrive);
    aBias    = std::make_unique<SA> (proc.apvts, PID::bias,    sBias);
    aOutTrim = std::make_unique<SA> (proc.apvts, PID::outTrim, sOutTrim);
    aInIron  = std::make_unique<SA> (proc.apvts, PID::inIron,  sInIron);
    aOutIron = std::make_unique<SA> (proc.apvts, PID::outIron, sOutIron);
    aHpf     = std::make_unique<SA> (proc.apvts, PID::hpf,     sHpf);

    // --- character segmented switch (TEKK | TUBE | CLONE -> indices 0 | 2 | 1) ---
    for (auto* btn : { &tekkBtn, &tubeBtn, &cloneBtn })
    {
        btn->setClickingTogglesState (true);
        btn->setColour (juce::TextButton::textColourOnId,  juce::Colour (LF::cTextHi));
        btn->setColour (juce::TextButton::textColourOffId, juce::Colour (LF::cTextDim));
        addAndMakeVisible (btn);
    }
    tekkBtn.getProperties() .set ("accent", (int) LF::cCyan);
    tubeBtn.getProperties() .set ("accent", (int) LF::cMagenta);
    cloneBtn.getProperties().set ("accent", (int) LF::cMagenta);
    tekkBtn.onClick  = [this] { setCharacter (0); };
    tubeBtn.onClick  = [this] { setCharacter (2); };
    cloneBtn.onClick = [this] { setCharacter (1); };
    {   // reflect the current choice immediately (no first-tick flash)
        const int ch0 = (int) proc.apvts.getRawParameterValue (PID::character)->load();
        tekkBtn .setToggleState (ch0 == 0, juce::dontSendNotification);
        tubeBtn .setToggleState (ch0 == 2, juce::dontSendNotification);
        cloneBtn.setToggleState (ch0 == 1, juce::dontSendNotification);
    }

    // clone curve loader
    loadBtn.getProperties().set ("accent", (int) LF::cMagenta);
    loadBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (LF::cTextDim));
    loadBtn.onClick = [this] { openCurveChooser(); };
    addAndMakeVisible (loadBtn);

    cloneNameLabel.setJustificationType (juce::Justification::centredRight);
    cloneNameLabel.setColour (juce::Label::textColourId, juce::Colour (LF::cTextDim));
    cloneNameLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    cloneNameLabel.setText ("clone: " + proc.getCloneName(), juce::dontSendNotification);
    addAndMakeVisible (cloneNameLabel);

    // --- footer ---
    autoGainBtn.setColour (juce::ToggleButton::textColourId, juce::Colour (LF::cTextDim));
    lowLatBtn  .setColour (juce::ToggleButton::textColourId, juce::Colour (LF::cTextDim));
    addAndMakeVisible (autoGainBtn);
    addAndMakeVisible (lowLatBtn);
    aAutoGain = std::make_unique<BA> (proc.apvts, PID::autoGain, autoGainBtn);
    aLowLat   = std::make_unique<BA> (proc.apvts, PID::lowLat,   lowLatBtn);

    osLabel.setJustificationType (juce::Justification::centredLeft);
    osLabel.setColour (juce::Label::textColourId, juce::Colour (LF::cTextDim));
    osLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (osLabel);
    osBox.addItem ("1x", 1); osBox.addItem ("2x", 2);
    osBox.addItem ("4x", 3); osBox.addItem ("8x", 4);
    addAndMakeVisible (osBox);
    aOs = std::make_unique<CA> (proc.apvts, PID::osFactor, osBox);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    setSize (760, 500);
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setMinimumSize (660, 440);
        c->setMaximumSize (1140, 760);
    }

    startTimerHz (30);
}

PreampEditor::~PreampEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PreampEditor::setupKnob (juce::Slider& s, juce::Label& lab,
                              const juce::String& text, const juce::String& suffix,
                              juce::Colour accent)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colour (LF::cTextHi));
    s.setTextValueSuffix (suffix);
    addAndMakeVisible (s);

    lab.setText (text, juce::dontSendNotification);
    lab.setJustificationType (juce::Justification::centred);
    lab.setColour (juce::Label::textColourId, juce::Colour (LF::cTextDim));
    lab.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (lab);
}

void PreampEditor::setCharacter (int index)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (
            proc.apvts.getParameter (PID::character)))
        if (c->getIndex() != index)
            *c = index;                                 // notifies host + processor
}

void PreampEditor::openCurveChooser()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Load a .tekkcurve clone capture", juce::File{}, "*.tekkcurve");
    const auto fcFlags = juce::FileBrowserComponent::openMode
                       | juce::FileBrowserComponent::canSelectFiles;
    // SafePointer guards against the editor closing while the dialog is open.
    juce::Component::SafePointer<PreampEditor> safe (this);
    chooser->launchAsync (fcFlags, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;                            // editor gone
        const auto file = fc.getResult();
        if (file == juce::File{}) return;                       // cancelled
        const bool ok = safe->proc.loadCloneCurveText (file.loadFileAsString().toStdString(),
                                                       file.getFileNameWithoutExtension());
        if (ok)
        {
            safe->setCharacter (1);                             // hear it immediately
            safe->cloneNameLabel.setText ("clone: " + safe->proc.getCloneName(), juce::dontSendNotification);
        }
        else
        {
            safe->cloneNameLabel.setText ("invalid .tekkcurve", juce::dontSendNotification);
        }
    });
}

void PreampEditor::timerCallback()
{
    inMeter.update();
    outMeter.update();

    const int ch = (int) proc.apvts.getRawParameterValue (PID::character)->load();
    tekkBtn .setToggleState (ch == 0, juce::dontSendNotification);
    cloneBtn.setToggleState (ch == 1, juce::dontSendNotification);
    tubeBtn .setToggleState (ch == 2, juce::dontSendNotification);

    // keep the clone name in sync (also catches host preset / state restores)
    cloneNameLabel.setText ("clone: " + proc.getCloneName(), juce::dontSendNotification);
}

void PreampEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.fillAll (juce::Colour (LF::cBgDeep));

    auto header = b.removeFromTop (kHeaderH);
    auto footer = b.removeFromBottom (kFooterH);

    g.setColour (juce::Colour (LF::cBgPanel));
    g.fillRect (header);
    g.fillRect (footer);

    // neon hairlines separating header / footer from the body
    g.setColour (juce::Colour (LF::cCyan).withAlpha (0.55f));
    g.fillRect (header.getX(), header.getBottom() - 1, header.getWidth(), 1);
    g.setColour (juce::Colour (LF::cMagenta).withAlpha (0.40f));
    g.fillRect (footer.getX(), footer.getY(), footer.getWidth(), 1);

    // title
    auto title = header.reduced (18, 0);
    g.setColour (juce::Colour (LF::cCyan));
    g.setFont (LF::titleFont (30.0f));
    auto tk = title.removeFromLeft (84);
    g.drawText ("TEKK", tk, juce::Justification::centredLeft);
    g.setColour (juce::Colour (LF::cTextHi));
    g.setFont (LF::titleFont (17.0f));
    g.drawText ("PREAMP", title.removeFromLeft (110), juce::Justification::centredLeft);
}

void PreampEditor::resized()
{
    auto b = getLocalBounds();
    auto header = b.removeFromTop (kHeaderH);
    auto footer = b.removeFromBottom (kFooterH);

    // header right: [ LOAD CLONE | TEKK | TUBE | CLONE ] over a clone-name strip
    {
        auto h     = header.reduced (14, 12);
        auto right = h.removeFromRight (340);
        auto top   = right.removeFromTop (26);
        auto sw    = top.removeFromRight (204);
        const int seg = sw.getWidth() / 3;
        tekkBtn .setBounds (sw.removeFromLeft (seg).reduced (2));
        tubeBtn .setBounds (sw.removeFromLeft (seg).reduced (2));
        cloneBtn.setBounds (sw.reduced (2));
        top.removeFromRight (8);
        loadBtn.setBounds (top.removeFromRight (90));
        right.removeFromTop (3);
        cloneNameLabel.setBounds (right);
    }

    // footer: AUTO GAIN | OVERSAMPLE | LOW LATENCY
    {
        auto f = footer.reduced (16, 13);
        autoGainBtn.setBounds (f.removeFromLeft (150));
        lowLatBtn  .setBounds (f.removeFromRight (160));
        osLabel.setBounds (f.removeFromLeft (94));
        osBox.setBounds (f.removeFromLeft (76).withSizeKeepingCentre (76, 26));
    }

    // body: meter strip on the right, knob grid on the left
    auto body = b.reduced (16, 14);
    auto meterStrip = body.removeFromRight (104);
    {
        meterStrip.removeFromLeft (10);
        const int mw = 40, gap = 14;
        inMeter .setBounds (meterStrip.removeFromLeft (mw));
        meterStrip.removeFromLeft (gap);
        outMeter.setBounds (meterStrip.removeFromLeft (mw));
    }

    // 4 columns x 2 rows; 7 knobs fill all but the last cell
    std::array<juce::Slider*, 7> sl { &sInTrim, &sDrive, &sBias, &sOutTrim,
                                      &sInIron, &sOutIron, &sHpf };
    std::array<juce::Label*, 7>  la { &lInTrim, &lDrive, &lBias, &lOutTrim,
                                      &lInIron, &lOutIron, &lHpf };
    const int cols = 4, rows = 2;
    const int cw = body.getWidth()  / cols;
    const int rh = body.getHeight() / rows;
    for (size_t i = 0; i < sl.size(); ++i)
    {
        const int r = (int) i / cols, c = (int) i % cols;
        auto cell = juce::Rectangle<int> (body.getX() + c * cw,
                                          body.getY() + r * rh, cw, rh).reduced (6);
        la[i]->setBounds (cell.removeFromTop (16));
        sl[i]->setBounds (cell);
    }
}
