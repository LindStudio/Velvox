#include "AssistantContextBuilder.h"
#include "AssistantSession.h"
#include "../PluginProcessor.h"

namespace velvox::assistant
{
    AssistantContext buildContext (VelvoxAudioProcessor& p)
    {
        AssistantContext c;
        c.values = AssistantSession::readValues (p.getState());

        const auto& a = p.getLatestAnalysis();
        const bool checkOn = c.get (pid::checkOn) > 0.5f;
        if (checkOn && a.enoughMaterial)
        {
            c.analysisValid = true;
            c.voicedSeconds = a.voicedSeconds;
            c.bandDb = a.bandDb;
            c.sibilanceVsVowelDb = a.sibilanceVsVowelDb;
            c.stereoKnown = true;
            c.stereoSource = a.stereo;
        }

        const auto& m = p.getChain().meters;
        c.correlationKnown = m.inputLufs.load() > -70.0f;   // only meaningful while audio plays
        c.correlation = m.correlation.load();

        const auto sc = p.getSidechainStatus();
        c.sidechainConnected = sc.connected;
        c.sidechainSignal = sc.hasSignal();
        c.sidechainRmsDb = sc.beatRmsDb;
        c.vocalRmsDb = sc.vocalRmsDb;

        c.macrosActive = p.readMacros().anyActive();
        return c;
    }
}
