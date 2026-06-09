#include "PluginProcessor.h"
#include "PluginEditor.h"

PreampProcessor::PreampProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    buildPlaceholderCloneCurve();
}

// ---- voicing-specific J-A params (this is the voicing pass, in code form) ----
static JilesAtherton::Params inputIron()
{
    // input transformer: tighter, lighter LF bias than the output
    return { 1.0, 0.12, 1.1e-3, 0.05, 0.30 };
}
static JilesAtherton::Params outputIron()
{
    // output transformer: heavier lows, a touch more loss (3rd-harmonic LF)
    return { 1.0, 0.14, 1.2e-3, 0.07, 0.22 };
}

void PreampProcessor::buildPlaceholderCloneCurve()
{
    // Stand-in for a real capture: asymmetric tanh blend sampled to a LUT.
    // Replace loadCurve() input with your slow-sweep capture of a target unit.
    const int N = 2048;
    cloneCurve.resize (N);
    for (int i = 0; i < N; ++i)
    {
        double x = cloneRange * (2.0 * i / (N - 1) - 1.0);
        double y = 0.5 * std::tanh (1.6 * x + 0.25)
                 + 0.5 * std::tanh (1.1 * x) - 0.5 * std::tanh (0.25);
        cloneCurve[(size_t) i] = (float) y;
    }
}

std::unique_ptr<ActiveStage> PreampProcessor::makeActiveStage (int characterChoice)
{
    if (characterChoice == 1)   // Clone
    {
        auto s = std::make_unique<LutCloneStage>();
        s->loadCurve (cloneCurve, cloneRange);
        return s;
    }
    return std::make_unique<TekkTanhStage>();   // TEKK parametric
}

void PreampProcessor::buildChains (int numChannels, double fs, int characterChoice)
{
    channels.clear();
    channels.reserve ((size_t) numChannels);

    const float hpfHz = apvts.getRawParameterValue (PID::hpf)->load();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        ChannelChain cc;

        auto inTrim = std::make_unique<TrimBlock>();        cc.inTrim = inTrim.get();
        auto inXf   = std::make_unique<TransformerBlock>(); cc.inXf   = &inXf->tf;
        auto actUp  = makeActiveStage (characterChoice);
        auto actBlk = std::make_unique<ActiveBlock>(std::move (actUp));
        cc.active   = actBlk->stage.get();
        auto dc     = std::make_unique<DCBlock>((double) hpfHz);
        auto outXf  = std::make_unique<TransformerBlock>(); cc.outXf  = &outXf->tf;
        auto outTr  = std::make_unique<TrimBlock>();        cc.outTrim = outTr.get();

        cc.inXf->setParams (inputIron());
        cc.outXf->setParams (outputIron());

        cc.chain.stages.push_back (std::move (inTrim));
        cc.chain.stages.push_back (std::move (inXf));
        cc.chain.stages.push_back (std::move (actBlk));
        cc.chain.stages.push_back (std::move (dc));
        cc.chain.stages.push_back (std::move (outXf));
        cc.chain.stages.push_back (std::move (outTr));

        cc.chain.prepare (fs);
        channels.push_back (std::move (cc));
    }
    builtCharacter = characterChoice;
}

void PreampProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int osChoice = (int) apvts.getRawParameterValue (PID::osFactor)->load();
    const bool lowLat  = apvts.getRawParameterValue (PID::lowLat)->load() > 0.5f;
    const int numCh    = getTotalNumOutputChannels();

    const size_t stages = (size_t) osChoice;   // 0->1x,1->2x,2->4x,3->8x
    using OS = juce::dsp::Oversampling<float>;
    oversampling = std::make_unique<OS>(
        (size_t) juce::jmax (1, numCh), stages,
        lowLat ? OS::filterHalfBandPolyphaseIIR     // min-phase, low latency
               : OS::filterHalfBandFIREquiripple);  // linear phase, more latency
    oversampling->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) oversampling->getLatencyInSamples());

    currentFs = sampleRate * (double) (1u << (unsigned) stages);
    builtOsChoice = osChoice; builtLowLat = lowLat;

    const int character = (int) apvts.getRawParameterValue (PID::character)->load();
    buildChains (numCh, currentFs, character);

    auto smoothSetup = [sampleRate] (juce::SmoothedValue<float>& s, float init)
    { s.reset (sampleRate, 0.02); s.setCurrentAndTargetValue (init); };
    smoothSetup (inGainSm, 1.0f);
    smoothSetup (outGainSm, 1.0f);
    smoothSetup (autoGainSm, 1.0f);
}

void PreampProcessor::updateLiveParams()
{
    const float drive = apvts.getRawParameterValue (PID::drive)->load();
    const float bias  = apvts.getRawParameterValue (PID::bias)->load();
    const float inI   = apvts.getRawParameterValue (PID::inIron)->load();
    const float outI  = apvts.getRawParameterValue (PID::outIron)->load();

    for (auto& cc : channels)
    {
        if (auto* t = dynamic_cast<TekkTanhStage*> (cc.active))
        { t->setDrive (drive); t->setBias (bias); }
        cc.inXf->setFluxDrive (inI);
        cc.outXf->setFluxDrive (outI);
    }
}

bool PreampProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in  = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono()
        || out == juce::AudioChannelSet::stereo();
}

void PreampProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // mandatory: near-unity poles breed subnormals

    const int numCh  = buffer.getNumChannels();
    const int numSmp = buffer.getNumSamples();

    // rebuild if character changed at runtime (allocates -> only on user change)
    const int character = (int) apvts.getRawParameterValue (PID::character)->load();
    if (character != builtCharacter)
        buildChains (numCh, currentFs, character);

    updateLiveParams();

    const float inGain  = juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (PID::inTrim)->load());
    const float outGain = juce::Decibels::decibelsToGain (
        apvts.getRawParameterValue (PID::outTrim)->load());
    const bool  autoG   = apvts.getRawParameterValue (PID::autoGain)->load() > 0.5f;
    inGainSm.setTargetValue (inGain);
    outGainSm.setTargetValue (outGain);

    // input-trim into the block + capture pre-RMS for auto-gain
    double preSumSq = 0.0;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int n = 0; n < numSmp; ++n)
        {
            const float g = inGainSm.getNextValue();
            d[n] *= g;
            preSumSq += (double) d[n] * d[n];
        }
        inGainSm.setCurrentAndTargetValue (inGain);   // re-arm per channel
    }

    // ---- oversampled nonlinear block ----
    juce::dsp::AudioBlock<float> block (buffer);
    auto osBlock = oversampling->processSamplesUp (block);

    const int osCh  = (int) osBlock.getNumChannels();
    const int osLen = (int) osBlock.getNumSamples();
    for (int ch = 0; ch < osCh && ch < (int) channels.size(); ++ch)
    {
        auto* d = osBlock.getChannelPointer ((size_t) ch);
        auto& cc = channels[(size_t) ch];
        for (int n = 0; n < osLen; ++n)
            d[n] = cc.chain.processSample (d[n]);
    }

    oversampling->processSamplesDown (block);

    // ---- output trim + auto-gain (matched-loudness color vs bypass) ----
    double postSumSq = 0.0;
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* r = buffer.getReadPointer (ch);
        for (int n = 0; n < numSmp; ++n) postSumSq += (double) r[n] * r[n];
    }

    float makeup = 1.0f;
    if (autoG && postSumSq > 1.0e-12)
        makeup = (float) std::sqrt (preSumSq / postSumSq);
    makeup = juce::jlimit (0.25f, 4.0f, makeup);
    autoGainSm.setTargetValue (makeup);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int n = 0; n < numSmp; ++n)
            d[n] *= outGainSm.getNextValue() * autoGainSm.getNextValue();
        outGainSm.setCurrentAndTargetValue (outGain);
        autoGainSm.setCurrentAndTargetValue (makeup);
    }
}

void PreampProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PreampProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PreampProcessor::createEditor()
{
    return new PreampEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PreampProcessor();
}
