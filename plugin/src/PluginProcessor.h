#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ParameterIDs.h"

// shared, JUCE-free DSP core (same headers the offline harness validates)
#include "../../dsp/PreampChain.h"
#include "../../dsp/ActiveStage.h"
#include "../../dsp/CloneCurve.h"
#include "DefaultCloneCurve.h"   // generated: tools/clone_capture.cpp bake

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

    // Swap the clone path's captured curve at runtime (e.g. a user-loaded
    // .tekkcurve from tools/clone_capture). Thread-safe; the chain is rebuilt on
    // the next processBlock, exactly like a character switch. A custom curve is
    // persisted in the plugin state. Returns false if the curve/text is invalid.
    bool loadCloneCurve     (const CloneCurve& c, const juce::String& name = "custom");
    bool loadCloneCurveText (const std::string& tekkcurveText, const juce::String& name = "custom");
    juce::String getCloneName();   // display name of the active clone curve

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

    // captured transfer curve for the clone path. Defaults to the baked-in
    // reference-unit capture (DefaultCloneCurve.h, produced by the offline
    // capture pipeline); replaceable at runtime via loadCloneCurve(). Guarded by
    // cloneLock because makeActiveStage() copies it during a (non-RT) rebuild.
    std::vector<float> cloneCurve;
    float cloneRange = 4.0f;
    juce::String cloneName { "default" };   // guarded by cloneLock
    bool  customClone = false;              // true once a user curve is loaded
    juce::SpinLock cloneLock;
    void loadDefaultCloneCurve();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreampProcessor)
};
