#include "PluginProcessor.h"
#include "PluginEditor.h"

PreampProcessor::PreampProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    loadDefaultCloneCurve();
}

// ---- voicing-specific J-A params (the voicing pass, in code form) ----------
//  Tuned against the offline harness (see harness/measure.cpp). With the
//  timestep-normalised TransformerStage these produce, per transformer:
//    * a monotonic 1/omega THD curve (LF coloured, HF clean),
//    * distortion that blooms with level (clean at nominal, iron when pushed),
//    * an odd-dominant (H3 > H2) spectrum, as a symmetric core should,
//    * a non-zero B-H loop (real memory),
//  and -- crucially -- identical character at every oversampling factor.
//  Pair them with the leak (LF bandwidth) set in buildChains().
static JilesAtherton::Params inputIron()
{
    // input transformer: tighter, lighter LF colour (~2.5% THD @40Hz nominal)
    return { 1.0, 0.12, 1.1e-3, 0.032, 0.70 };
}
static JilesAtherton::Params outputIron()
{
    // output transformer: heavier lows, more 3rd-harmonic (~5.6% THD @40Hz nom.)
    return { 1.0, 0.13, 1.2e-3, 0.030, 0.62 };
}
// per-transformer LF bandwidth (Hz): the iron's low-frequency rolloff. Lower =
// more sub-bass reaches the core = heavier lows, so the output sits below input.
static constexpr double kInputLeakHz  = 20.0;
static constexpr double kOutputLeakHz = 15.0;

void PreampProcessor::loadDefaultCloneCurve()
{
    // The baked-in capture of the reference unit (tools/clone_capture bake).
    const juce::SpinLock::ScopedLockType lock (cloneLock);
    cloneCurve.assign (DefaultCloneCurve::kCurve,
                       DefaultCloneCurve::kCurve + DefaultCloneCurve::kCurveN);
    cloneRange = DefaultCloneCurve::kCurveRange;
}

bool PreampProcessor::loadCloneCurve (const CloneCurve& c)
{
    if (! c.valid()) return false;
    {
        const juce::SpinLock::ScopedLockType lock (cloneLock);
        cloneCurve = c.samples;
        cloneRange = c.range;
    }
    builtCharacter = -1;            // force a rebuild on the next block (new curve)
    return true;
}

bool PreampProcessor::loadCloneCurveText (const std::string& tekkcurveText)
{
    return loadCloneCurve (CloneCurve::parse (tekkcurveText));
}

std::unique_ptr<ActiveStage> PreampProcessor::makeActiveStage (int characterChoice)
{
    if (characterChoice == 1)   // Clone
    {
        auto s = std::make_unique<LutCloneStage>();
        const juce::SpinLock::ScopedLockType lock (cloneLock);
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
        cc.inXf->setLeakHz (kInputLeakHz);
        cc.outXf->setParams (outputIron());
        cc.outXf->setLeakHz (kOutputLeakHz);

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

    // input-trim into the block + capture pre-RMS for auto-gain + IN meter peak
    double preSumSq = 0.0;
    float  inPeak   = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int n = 0; n < numSmp; ++n)
        {
            const float g = inGainSm.getNextValue();
            d[n] *= g;
            preSumSq += (double) d[n] * d[n];
            inPeak = juce::jmax (inPeak, std::abs (d[n]));
        }
        inGainSm.setCurrentAndTargetValue (inGain);   // re-arm per channel
    }
    meterIn.store (inPeak, std::memory_order_relaxed);

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

    float outPeak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int n = 0; n < numSmp; ++n)
        {
            d[n] *= outGainSm.getNextValue() * autoGainSm.getNextValue();
            outPeak = juce::jmax (outPeak, std::abs (d[n]));
        }
        outGainSm.setCurrentAndTargetValue (outGain);
        autoGainSm.setCurrentAndTargetValue (makeup);
    }
    meterOut.store (outPeak, std::memory_order_relaxed);
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
