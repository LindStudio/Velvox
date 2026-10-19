#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/UserSettings.h"
#include "Assistant/IntentParser.h"
#include "Assistant/AssistantPlanner.h"
#include "Assistant/AssistantContextBuilder.h"

// Prints what the Smart Assistant makes of a sentence (for reviewing the rules):
//   VelvoxTests --assistant "Weniger Tiefen, mehr Hall"
static int printAssistantPlan (const juce::String& text)
{
    using namespace velvox::assistant;
    VelvoxAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    const auto parsed = IntentParser::parse (text);
    const auto plan = AssistantPlanner::plan (parsed, buildContext (proc));
    juce::String out;
    out << "Input: " << text << "\n";
    for (auto& c : plan.changes)
        out << "  [" << (c.selected ? "x" : " ") << "] " << c.module << " - " << c.title << ": " << c.paramName << " "
            << c.currentText << " -> " << c.proposedText << (c.measured ? "  (Messung)" : "  (Regel)") << "\n      " << c.reason << "\n";
    for (auto& n : plan.notes)
        out << "  * " << n.text << "\n";
    std::printf ("%s\n", out.toRawUTF8());
    return 0;
}

struct StdoutLogger final : juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::printf ("%s\n", message.toRawUTF8());
        std::fflush (stdout);
    }
};

// Renders the real editor to a PNG (for visual review without a DAW):
//   VelvoxTests --snapshot out.png [presetName] [width] [beginner|pro] [moduleId|-1] [overlay]
// overlay: "help:<page>", "welcome", "tutorial:<step>", "bypass", "hide:<key,key>", "focus:<moduleId>", "edit" (changes a value)
static int renderSnapshot (const juce::File& file, const juce::String& presetName, int width,
                           const juce::String& mode, int selection, const juce::String& overlay)
{
    // Never touch the real user preferences; the welcome card only when asked for
    const auto prefs = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("velvox-snapshot.settings");
    prefs.deleteFile();
    velvox::ui::settings::useFileForTesting (prefs);
    velvox::ui::settings::set (velvox::ui::settings::tutorialDone, overlay != "welcome");

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

    if (overlay == "bypass")
        proc.getState().getParameter ("bypass")->setValueNotifyingHost (1.0f);
    if (overlay.startsWith ("hide:"))
        proc.getState().state.setProperty ("uiHiddenModules", overlay.fromFirstOccurrenceOf (":", false, false), nullptr);
    if (overlay == "edit")
        proc.getState().getParameter ("compRatio")->setValueNotifyingHost (0.6f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (width > 0)
        editor->setSize (width, width * velvox::ui::baseHeight / velvox::ui::baseWidth);

    if (auto* ed = dynamic_cast<VelvoxAudioProcessorEditor*> (editor.get()))
    {
        if (overlay.startsWith ("help:"))
            ed->showHelp (overlay.fromFirstOccurrenceOf (":", false, false));
        if (overlay.startsWith ("tutorial:"))
        {
            ed->startTutorial (false);
            ed->getTutorial().goTo (overlay.fromFirstOccurrenceOf (":", false, false).getIntValue());
        }
        if (overlay.startsWith ("assistant"))
        {
            // "assistant" = empty page, "assistant:<text>" = analysed, "assistant!:<text>" = applied
            ed->setAssistantOpen (true);
            const auto text = overlay.fromFirstOccurrenceOf (":", false, false);
            if (text.isNotEmpty())
            {
                ed->getAssistantView().setText (text);
                ed->getAssistantView().analyseNow();
                if (overlay.startsWith ("assistant!"))
                    ed->getAssistantView().applySelected();
            }
        }
        if (overlay.startsWith ("focus:"))
            ed->getChainView().onSelect (overlay.fromFirstOccurrenceOf (":", false, false).getIntValue());
    }

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

    if (argc >= 3 && juce::String (argv[1]) == "--assistant")
        return printAssistantPlan (juce::String::fromUTF8 (argv[2]));

    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return renderSnapshot (juce::File (juce::String (argv[2])),
                               argc >= 4 ? juce::String (argv[3]) : juce::String(),
                               argc >= 5 ? juce::String (argv[4]).getIntValue() : 0,
                               argc >= 6 ? juce::String (argv[5]) : juce::String(),
                               argc >= 7 ? juce::String (argv[6]).getIntValue() : 0,
                               argc >= 8 ? juce::String (argv[7]) : juce::String());

    // Tests use their own preferences file (tutorial state, saved layouts)
    const auto prefs = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("velvox-tests.settings");
    prefs.deleteFile();
    velvox::ui::settings::useFileForTesting (prefs);

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
