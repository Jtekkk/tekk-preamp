#pragma once
#include "ActiveStage.h"
#include "TransformerStage.h"
#include "DCBlocker.h"
#include <memory>
#include <vector>

// Polymorphic stage base. The chain runs a vector of these in order, inside
// the oversampled block in the real plugin. JUCE-free so the harness and the
// plugin share the exact same DSP.
struct PreampStage
{
    virtual ~PreampStage() = default;
    virtual void  prepare (double fs) = 0;
    virtual void  reset()             = 0;
    virtual float processSample (float x) = 0;
};

// --- wrappers so each block fits the common interface --------------------
struct TransformerBlock : PreampStage
{
    TransformerStage tf;
    void  prepare (double fs) override { tf.prepare (fs); }
    void  reset()             override { tf.reset(); }
    float processSample (float x) override { return tf.processSample (x); }
};

struct ActiveBlock : PreampStage
{
    std::unique_ptr<ActiveStage> stage;     // TEKK tanh OR captured-curve clone
    explicit ActiveBlock (std::unique_ptr<ActiveStage> s) : stage (std::move (s)) {}
    void  prepare (double fs) override { stage->prepare (fs); }
    void  reset()             override { stage->reset(); }
    float processSample (float x) override { return stage->processSample (x); }
};

struct DCBlock : PreampStage
{
    DCBlocker dc;
    double fc;
    explicit DCBlock (double cutoff = 6.0) : fc (cutoff) {}
    void  prepare (double fs) override { dc.prepare (fs, fc); }
    void  reset()             override { dc.reset(); }
    float processSample (float x) override { return dc.processSample (x); }
};

struct TrimBlock : PreampStage
{
    float gain = 1.0f;
    void  prepare (double) override {}
    void  reset()         override {}
    float processSample (float x) override { return x * gain; }
};

// --- the chain -----------------------------------------------------------
//  input trim -> input transformer -> active stage -> DC block
//             -> output transformer -> output trim
//  (auto-gain / matched-bypass and oversampling wrap this in the plugin)
class PreampChain
{
public:
    std::vector<std::unique_ptr<PreampStage>> stages;

    void prepare (double fs) { for (auto& s : stages) s->prepare (fs); }
    void reset()             { for (auto& s : stages) s->reset(); }

    inline float processSample (float x)
    {
        for (auto& s : stages) x = s->processSample (x);
        return x;
    }
};
