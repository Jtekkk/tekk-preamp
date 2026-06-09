#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace PID
{
    // these strings are the stable APVTS identifiers; don't rename after release
    inline constexpr const char* inTrim   = "inTrim";
    inline constexpr const char* drive    = "drive";
    inline constexpr const char* character= "character";   // 0 = TEKK, 1 = Clone
    inline constexpr const char* bias     = "bias";        // even-harmonic amount
    inline constexpr const char* inIron   = "inIron";      // input xfmr flux drive
    inline constexpr const char* outIron  = "outIron";     // output xfmr flux drive
    inline constexpr const char* hpf      = "hpf";         // inter-stage HPF Hz
    inline constexpr const char* outTrim  = "outTrim";
    inline constexpr const char* autoGain = "autoGain";
    inline constexpr const char* osFactor = "osFactor";    // 0:1x 1:2x 2:4x 3:8x
    inline constexpr const char* lowLat   = "lowLat";      // IIR (min-phase) OS
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout p;
    auto db = [] (float lo, float hi, float def)
    { return NormalisableRange<float> (lo, hi, 0.01f); };

    p.add (std::make_unique<AudioParameterFloat>(
        ParameterID { PID::inTrim, 1 }, "Input Trim", db (-24, 24, 0), 0.0f));
    p.add (std::make_unique<AudioParameterFloat>(
        ParameterID { PID::drive, 1 }, "Drive",
        NormalisableRange<float> (1.0f, 30.0f, 0.01f, 0.5f), 3.0f));
    p.add (std::make_unique<AudioParameterChoice>(
        ParameterID { PID::character, 1 }, "Character",
        StringArray { "TEKK (parametric)", "Clone (captured)" }, 0));
    p.add (std::make_unique<AudioParameterFloat>(
        ParameterID { PID::bias, 1 }, "Bias / Warmth",
        NormalisableRange<float> (0.0f, 1.2f, 0.001f), 0.0f));
    // flux drive into each core; calibrated for the normalised TransformerStage
    // (see harness). ~0.0009/0.0013 = musical defaults; top of range = slammed.
    p.add (std::make_unique<AudioParameterFloat>(
        ParameterID { PID::inIron, 1 }, "Input Iron",
        NormalisableRange<float> (0.0f, 0.0035f, 0.00001f, 0.5f), 0.0009f));
    p.add (std::make_unique<AudioParameterFloat>(
        ParameterID { PID::outIron, 1 }, "Output Iron",
        NormalisableRange<float> (0.0f, 0.0035f, 0.00001f, 0.5f), 0.0013f));
    p.add (std::make_unique<AudioParameterFloat>(
        ParameterID { PID::hpf, 1 }, "HPF",
        NormalisableRange<float> (2.0f, 40.0f, 0.1f), 6.0f));
    p.add (std::make_unique<AudioParameterFloat>(
        ParameterID { PID::outTrim, 1 }, "Output Trim", db (-24, 24, 0), 0.0f));
    p.add (std::make_unique<AudioParameterBool>(
        ParameterID { PID::autoGain, 1 }, "Auto Gain", true));
    p.add (std::make_unique<AudioParameterChoice>(
        ParameterID { PID::osFactor, 1 }, "Oversampling",
        StringArray { "1x", "2x", "4x", "8x" }, 2));
    p.add (std::make_unique<AudioParameterBool>(
        ParameterID { PID::lowLat, 1 }, "Low Latency (IIR)", false));

    return p;
}
