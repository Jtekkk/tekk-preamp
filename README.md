# TEKK Preamp

A character/coloration preamp in JUCE. One architecture, two voicings behind a
single swappable `ActiveStage` interface:

  * TEKK (parametric) -- biased tanh + 1st-order ADAA. `bias` controls even
    harmonics (Chebyshev projection: an odd function has exactly zero even
    coefficients, so bias is the "tube warmth" dial).
  * Clone (captured)  -- a measured static transfer curve played through a LUT,
    with the curve's own antiderivative table for ADAA. Higher-fidelity clone
    options (WDF Koren triode, RTNeural LSTM) slot in behind the same interface.

Both paths are wrapped by transformer coloration stages built on a
Jiles-Atherton hysteresis core (genuine B-H memory, not a static curve), solved
with an implicit trapezoidal / Newton step so it stays stable at low
oversampling.

## Layout

    dsp/            JUCE-free DSP core (shared by harness AND plugin)
      ActiveStage.h     gain element: TekkTanhStage + LutCloneStage (ADAA)
      JilesAtherton.h   magnetic hysteresis, implicit Newton solve
      TransformerStage.h  integrate -> J-A -> differentiate (1/omega flux)
      DCBlocker.h       inter-stage HPF
      PreampChain.h     polymorphic stage chain
    harness/        offline validation (compiles & runs anywhere)
      measure.cpp       four experiments (see below)
      SimpleFFT.h       radix-2 FFT
    plugin/         JUCE plugin
      CMakeLists.txt    fetches JUCE 8.0.4 (or set JUCE_DIR)
      src/              processor, params, placeholder editor

## Build & run the harness

    g++ -O2 -std=c++17 harness/measure.cpp -o measure && ./measure

Validated there (measured, not asserted):
  [1] bias=0 -> even harmonics at the numerical floor; rise monotonically w/ bias
  [2] 1st-order ADAA -> ~12 dB lower alias floor at equal sample rate
  [3] transformer THD falls with frequency (LF flux ~ 1/omega) AND blooms with
      level (clean at nominal, iron when pushed)
  [4] J-A B-H loop has nonzero area (real memory)
  [5] transformer character is sample-rate independent (dt-normalised flux ->
      same voicing at 1x..8x oversampling)

## Build the plugin

    cd plugin
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    # builds VST3 / AU / Standalone

To use a local JUCE checkout instead of FetchContent, set JUCE_DIR and swap the
FetchContent block in CMakeLists.txt for add_subdirectory(${JUCE_DIR} juce).

## Done
  * Voiced the iron: inputIron/outputIron in PluginProcessor.cpp are now tuned
    against the harness (input lighter, output heavier in the lows), with the
    flux integrator dt-normalised so the voicing holds at every oversampling
    factor. The IN/OUT IRON knobs scale the flux drive around those points.
  * UI: a custom dark/cyberpunk editor (TekkLookAndFeel + PluginEditor) replaces
    the generic panel -- rotary knobs, TEKK/CLONE switch, live I/O meters.
    tools/RenderUI.cpp rasterises it to a PNG headlessly (-DTEKK_BUILD_UITOOL=ON).

## Open work
  * Clone capture pipeline: replace buildPlaceholderCloneCurve() with a real
    slow-sweep capture of a target unit.
