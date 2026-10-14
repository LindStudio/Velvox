#include "PluginProcessor.h"

struct StdoutLogger final : juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::printf ("%s\n", message.toRawUTF8());
        std::fflush (stdout);
    }
};

// Renders the real editor to a PNG (for visual review without a DAW):
//   VelvoxTests --snapshot out.png [presetName] [width] [beginner|pro] [moduleId|-1]
static int renderSnapshot (const juce::File& file, const juce::String& presetName, int width,
                           const juce::String& mode, int selection)
{
    VelvoxAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    proc.getState().state.setProperty ("uiMode", mode.isEmpty() ? juce::String ("beginner") : mode, nullptr);
    proc.getState().state.setProperty ("uiSelection", selection, nullptr);

    auto& pm = proc.getPresetManager();
    for (int i = 0; i < pm.getEntries().size(); ++i)
        if (pm.getEntries()[i].name == presetName)
            pm.loadPreset (i);

    // Feed some audio so meters show activity
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random r (3);
    for (int b = 0; b < 40; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, 0.4f * (r.nextFloat() * 2.0f - 1.0f));
        proc.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (width > 0)
        editor->setSize (width, width * 800 / 1280);

    // Let the editor timer run a few frames
    for (int i = 0; i < 8; ++i)
    {
        proc.processBlock (buffer, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::printf ("%s %s\n", ok ? "Wrote" : "FAILED to write", file.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for APVTS / parameter listeners

    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return renderSnapshot (juce::File (juce::String (argv[2])),
                               argc >= 4 ? juce::String (argv[3]) : juce::String(),
                               argc >= 5 ? juce::String (argv[4]).getIntValue() : 0,
                               argc >= 6 ? juce::String (argv[5]) : juce::String(),
                               argc >= 7 ? juce::String (argv[6]).getIntValue() : 0);

    StdoutLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }

    std::printf ("\n==== %d checks passed, %d failed ====\n", passes, failures);
    juce::Logger::setCurrentLogger (nullptr);
    return failures == 0 ? 0 : 1;
}
