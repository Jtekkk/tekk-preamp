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
      CloneCurve.h      captured-curve file format (.tekkcurve) + (de)serialise
    harness/        offline validation (compiles & runs anywhere)
      measure.cpp       five experiments (see below)
      SimpleFFT.h       radix-2 FFT
    tools/          JUCE-free dev tooling
      clone_capture.cpp clone capture pipeline (signal/extract/bake/selftest)
      WavIO.h           tiny WAV read/write
    plugin/         JUCE plugin
      CMakeLists.txt    fetches JUCE 8.0.4 (or set JUCE_DIR)
      src/              processor, params, dark/cyberpunk editor + LookAndFeel
      src/DefaultCloneCurve.h  generated: the baked-in clone capture

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

## Clone capture (cloning a real unit)

    g++ -O2 -std=c++17 -I dsp -I harness -I tools \
        tools/clone_capture.cpp -o clone_capture
    ./clone_capture selftest               # simulated round-trip + assertions

    ./clone_capture signal sweep.wav       # 1) make the test signal
    #                                        2) play it through the target unit,
    #                                           record the output -> rec.wav
    ./clone_capture extract sweep.wav rec.wav clone.tekkcurve   # 3) extract curve
    ./clone_capture bake plugin/src/DefaultCloneCurve.h         # bake -> default

The extractor time-aligns the recording (latency search), bins output by input
level over the whole sweep, and averages -- which also collapses the unit's thin
reactive hysteresis into the mean static curve (the iron memory is modelled
separately by the J-A cores around the clone stage). selftest reconstructs a
known device from a noisy, delayed recording to <1e-3 rms and matches its 1 kHz
THD to <0.05%. The plugin loads .tekkcurve at runtime via loadCloneCurveText().

## Done
  * Voiced the iron: inputIron/outputIron in PluginProcessor.cpp are now tuned
    against the harness (input lighter, output heavier in the lows), with the
    flux integrator dt-normalised so the voicing holds at every oversampling
    factor. The IN/OUT IRON knobs scale the flux drive around those points.
  * UI: a custom dark/cyberpunk editor (TekkLookAndFeel + PluginEditor) replaces
    the generic panel -- rotary knobs, TEKK/CLONE switch, live I/O meters.
    tools/RenderUI.cpp rasterises it to a PNG headlessly (-DTEKK_BUILD_UITOOL=ON).
  * Clone capture pipeline: tools/clone_capture.cpp turns a recorded sweep of a
    target unit into a .tekkcurve LUT; the default clone is now a baked capture
    (DefaultCloneCurve.h) rather than a hand-written placeholder.

## Open work
  * Higher-fidelity clone options behind the same ActiveStage seam: WDF Koren
    triode, or a small RTNeural LSTM trained on device captures.
  * UI to load a user .tekkcurve at runtime (loadCloneCurveText is already wired).
