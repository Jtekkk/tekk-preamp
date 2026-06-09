// ============================================================================
//  RenderUI : dev tooling, NOT part of the shipped plugin.
//
//  Instantiates the real plugin editor and rasterises it to a PNG using JUCE's
//  software renderer -- no audio host and no X display required. Used to eyeball
//  the front end from CI / headless boxes.
//
//  Build:  cmake -B build -DTEKK_BUILD_UITOOL=ON && cmake --build build
//  Run:    ./TEKKRenderUI out.png [width height]
// ============================================================================
#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI guiInit;

    const juce::String outPath = (argc > 1) ? juce::String (argv[1]) : juce::String ("tekk-ui.png");
    const int w = (argc > 3) ? juce::String (argv[2]).getIntValue() : 760;
    const int h = (argc > 3) ? juce::String (argv[3]).getIntValue() : 500;

    PreampProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // Pose the controls into musical positions BEFORE the editor exists: the
    // APVTS slider attachments read the current value on construction, so the
    // knobs come up posed without having to pump the message loop.
    auto pose = [&] (const char* id, float norm)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    };
    pose (PID::inTrim,  0.62f);
    pose (PID::drive,   0.46f);
    pose (PID::bias,    0.32f);
    pose (PID::outTrim, 0.44f);
    pose (PID::inIron,  0.55f);
    pose (PID::outIron, 0.66f);
    pose (PID::hpf,     0.22f);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setBounds (0, 0, w, h);

    // let any pending async UI updates / one timer tick settle
    juce::MessageManager::getInstance()->runDispatchLoopUntil (80);

    juce::Image img (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (img);
        ed->paintEntireComponent (g, false);
    }

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (outPath);
    out.deleteFile();
    if (auto os = out.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (img, *os);
        os->flush();
        std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), w, h);
        return 0;
    }

    std::printf ("failed to open %s for writing\n", out.getFullPathName().toRawUTF8());
    return 1;
}
