#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Placeholder editor: a generic parameter panel so the plugin is usable and
// testable immediately. The real dark/cyberpunk UI is the frontend pass --
// swap this for a custom juce::Component with your LookAndFeel and meters.
class PreampEditor : public juce::AudioProcessorEditor
{
public:
    explicit PreampEditor (PreampProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p), generic (p)
    {
        addAndMakeVisible (generic);
        setSize (420, 560);
        setResizable (true, true);
    }

    void resized() override { generic.setBounds (getLocalBounds()); }

private:
    PreampProcessor& proc;
    juce::GenericAudioProcessorEditor generic;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreampEditor)
};
