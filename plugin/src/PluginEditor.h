#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "TekkLookAndFeel.h"

// ============================================================================
//  LevelMeter : a vertical neon bar fed by one of the processor's atomic peak
//  values. Ballistics live here (instant attack, eased release, a falling peak
//  cap) so the audio thread only ever stores a raw block peak.
// ============================================================================
class LevelMeter : public juce::Component
{
public:
    LevelMeter (std::atomic<float>& src, juce::String caption)
        : source (src), label (std::move (caption)) {}

    void update();                                  // called from the editor timer
    void paint (juce::Graphics&) override;

private:
    static float normDb (float peak);

    std::atomic<float>& source;
    juce::String label;
    float level = 0.0f, peakHold = 0.0f;
    int   holdFrames = 0;
};

// ============================================================================
//  PreampEditor : the dark / cyberpunk front end. Replaces the generic panel.
// ============================================================================
class PreampEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PreampEditor (PreampProcessor&);
    ~PreampEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& text,
                    const juce::String& suffix, juce::Colour accent);
    void setCharacter (int index);

    PreampProcessor& proc;
    TekkLookAndFeel  lnf;

    // knobs + their name labels
    juce::Slider sInTrim, sDrive, sBias, sOutTrim, sInIron, sOutIron, sHpf;
    juce::Label  lInTrim, lDrive, lBias, lOutTrim, lInIron, lOutIron, lHpf;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SA> aInTrim, aDrive, aBias, aOutTrim, aInIron, aOutIron, aHpf;

    // character segmented switch (manual sync to the choice param)
    juce::TextButton tekkBtn { "TEKK" }, cloneBtn { "CLONE" };

    // clone curve loader: pick a .tekkcurve, hand it to the processor
    juce::TextButton loadBtn { "LOAD CLONE" };
    juce::Label      cloneNameLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    void openCurveChooser();

    // footer controls
    juce::ToggleButton autoGainBtn { "AUTO GAIN" }, lowLatBtn { "LOW LATENCY" };
    juce::ComboBox     osBox;
    juce::Label        osLabel { {}, "OVERSAMPLE" };
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<BA> aAutoGain, aLowLat;
    std::unique_ptr<CA> aOs;

    LevelMeter inMeter  { proc.meterIn,  "IN"  };
    LevelMeter outMeter { proc.meterOut, "OUT" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreampEditor)
};
