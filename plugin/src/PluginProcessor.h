#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ParameterIDs.h"

// shared, JUCE-free DSP core (same headers the offline harness validates)
#include "../../dsp/PreampChain.h"
#include "../../dsp/ActiveStage.h"

class PreampProcessor : public juce::AudioProcessor
{
public:
    PreampProcessor();
    ~PreampProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TEKK Preamp"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createLayout() };

    // --- metering: written on the audio thread, polled by the editor's timer.
    //  IN  = peak driving the chain (post input-trim, so it tracks the knob);
    //  OUT = peak leaving the chain (post output-trim + auto-gain).
    std::atomic<float> meterIn  { 0.0f };
    std::atomic<float> meterOut { 0.0f };

private:
    // typed handles into one channel's chain so we can push live params each block
    struct ChannelChain
    {
        PreampChain      chain;
        TrimBlock*       inTrim  = nullptr;
        TransformerStage* inXf   = nullptr;
        ActiveStage*     active  = nullptr;
        TransformerStage* outXf  = nullptr;
        TrimBlock*       outTrim = nullptr;
    };

    void buildChains (int numChannels, double fs, int characterChoice);
    std::unique_ptr<ActiveStage> makeActiveStage (int characterChoice);
    void updateLiveParams();

    std::vector<ChannelChain> channels;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    double currentFs = 44100.0;
    int    builtCharacter = -1;
    int    builtOsChoice  = -1;
    bool   builtLowLat    = false;

    juce::SmoothedValue<float> inGainSm, outGainSm, autoGainSm;

    // captured transfer curve for the clone path (placeholder: a measured curve
    // would be loaded from a resource / sweep). Here: an asymmetric soft clip so
    // the clone path is audibly distinct and fully functional out of the box.
    std::vector<float> cloneCurve;
    float cloneRange = 4.0f;
    void buildPlaceholderCloneCurve();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreampProcessor)
};
