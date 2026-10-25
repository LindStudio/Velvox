// VOCAL CHECK on real dry vocals (VocalSet v1.1 validation singers + the MYLK dry chorus).
// The audio is never in the repository: the test logs a note and passes when the files are
// missing. The full calibration (all 28 validation takes, precision / recall) is
// `VelvoxTests --check-eval <outDir> <list.txt>`; this keeps a small, fast subset in the suite.

#include "TestHelpers.h"
#include "CheckEval.h"

using namespace velvox;
using namespace velvox::test;

class VocalCheckRealDataTests final : public juce::UnitTest
{
public:
    VocalCheckRealDataTests() : juce::UnitTest ("Headliner Vocal Check real vocals", "Headliner") {}

    void runTest() override
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const auto vocalSet = home.getChildFile ("dev/velvox-data/sources/vocalset11/FULL");
        const auto mylk = home.getChildFile ("dev/velvox-bench/2026-10-24/hoertest_original_vs_0.4/08_A.wav");
        const juce::File files[] {
            vocalSet.getChildFile ("female7/excerpts/straight/f7_row_straight.wav"),
            vocalSet.getChildFile ("female8/excerpts/straight/f8_caro_straight.wav"),
            vocalSet.getChildFile ("male8/excerpts/straight/m8_dona_straight.wav"),
            vocalSet.getChildFile ("male9/excerpts/straight/m9_row_straight.wav"),
            mylk,
        };

        beginTest ("Real vocals: injected rumble and plosives are always found, never on the clean takes");
        {
            int present = 0, resonances = 0;
            for (auto& file : files)
            {
                const auto v = loadVocal (file, 24.0);
                if (! v.has_value())
                    continue;
                ++present;
                const auto clean = analyseCaptured (*v);
                expect (clean.enoughMaterial, file.getFileName());
                expect (detection (clean, Injection::rumble) == nullptr && detection (clean, Injection::plosives) == nullptr,
                        "no low-end finding on the clean take " + file.getFileName());
                for (auto kind : { Injection::rumble, Injection::plosives })
                    expect (detection (analyseCaptured (inject (*v, kind)), kind) != nullptr,
                            juce::String (injectionName (kind)) + " found in " + file.getFileName());
                resonances += detection (analyseCaptured (inject (*v, Injection::resonance)), Injection::resonance) != nullptr ? 1 : 0;
            }
            if (present == 0)
            {
                logMessage ("  real vocals not found (velvox-data / velvox-bench) - skipped");
                return;
            }
            logMessage ("  " + juce::String (resonances) + " of " + juce::String (present) + " injected 3.5 kHz resonances found");
            expectGreaterOrEqual (resonances * 5, present * 2, "at least 40 % of the resonances");
        }

        beginTest ("Real vocals: the sibilant MYLK chorus is told to de-ess; the automatic mix leaves fewer problems");
        {
            const auto v = loadVocal (mylk, 24.0);
            if (! v.has_value())
                return;
            const auto clean = analyseCaptured (*v);
            bool sibilance = false;
            for (auto& f : clean.findings)
                sibilance = sibilance || f.id == "sibilance";
            expect (sibilance, "MYLK: sibilance (the listening test asked for a de-esser)");

            for (auto& file : { files[3], mylk })
            {
                const auto take = loadVocal (file, 24.0);
                if (! take.has_value())
                    continue;
                const auto before = analyseCaptured (*take);
                const auto mixed = autoMixed (*take);
                expect (mixed.has_value(), "auto mix ran");
                if (! mixed.has_value())
                    continue;
                const auto after = analyseCaptured (*mixed);
                logMessage ("  " + file.getFileName() + ": score " + juce::String (before.score) + " -> " + juce::String (after.score) + ", findings "
                            + juce::String ((int) before.findings.size()) + " -> " + juce::String ((int) after.findings.size()));
                expectGreaterOrEqual (after.score, before.score, "the mixed take scores at least as well");
                expectLessOrEqual ((int) after.findings.size(), (int) before.findings.size(), "no more findings after the mix");
            }
        }
    }
};

static VocalCheckRealDataTests vocalCheckRealDataTests;
