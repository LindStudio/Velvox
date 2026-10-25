#include "MacroMapper.h"

namespace velvox
{
    namespace
    {
        // Host automation drives the mapping: plain values, no gestures (no undo, no touch on other lanes)
        eqbands::Access plainAccess (juce::AudioProcessorValueTreeState& apvts)
        {
            auto a = eqbands::accessFor (apvts);
            a.set = [&apvts] (const juce::String& id, float value)
            {
                auto* p = apvts.getParameter (id);
                if (p == nullptr || ! std::isfinite (value))
                    return;
                const float norm = p->convertTo0to1 (value);
                if (std::abs (norm - p->getValue()) >= 1.0e-7f)
                    p->setValueNotifyingHost (norm);
            };
            return a;
        }
    }

    const std::array<const char*, dsp::numMacros>& MacroMapper::paramIds()
    {
        // Indexed by dsp::MacroId
        static const std::array<const char*, dsp::numMacros> ids { pid::macroPresence, pid::macroWarmth, pid::macroAggression,
                                                                   pid::macroWidth, pid::macroSpace, pid::macroDensity };
        return ids;
    }

    bool MacroMapper::isMacroParam (const juce::String& id)
    {
        for (auto* m : paramIds())
            if (id == m)
                return true;
        return false;
    }

    MacroMapper::MacroMapper (juce::AudioProcessorValueTreeState& state, Hooks h)
        : apvts (state), hooks (std::move (h))
    {
        for (size_t i = 0; i < raw.size(); ++i)
            raw[i] = apvts.getRawParameterValue (paramIds()[i]);
        for (const auto& id : macrobake::touchedParamIds())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
                written.push_back (p);
        const auto& params = apvts.processor.getParameters();
        for (int i = 0; i < params.size(); ++i)
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[i]); rp != nullptr && isMacroParam (rp->getParameterID()))
                macroIndices.push_back (i);
        syncPositions();
    }

    void MacroMapper::syncPositions()
    {
        for (size_t i = 0; i < raw.size(); ++i)
            positions[i] = raw[i] != nullptr ? raw[i]->load() * 0.01f : 0.0f;
        anchor.valid = false;
    }

    bool MacroMapper::isMacroIndex (int index) const
    {
        return std::find (macroIndices.begin(), macroIndices.end(), index) != macroIndices.end();
    }

    void MacroMapper::gestureBegan (int index)
    {
        if (isMacroIndex (index))
            ++openGestures;
    }

    void MacroMapper::gestureEnded (int index)
    {
        if (isMacroIndex (index))
            openGestures = std::max (0, openGestures - 1);
    }

    std::vector<float> MacroMapper::captureWritten() const
    {
        std::vector<float> v;
        v.reserve (written.size());
        for (auto* p : written)
            v.push_back (p->getValue());
        return v;
    }

    void MacroMapper::update()
    {
        if (busy)
            return;
        const juce::ScopedValueSetter<bool> guard (busy, true);
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == nullptr)
                continue;
            const float now = raw[i]->load() * 0.01f;
            if (now == positions[i])
                continue;
            const float from = positions[i];
            positions[i] = now;
            if (hooks.isApplyingState != nullptr && hooks.isApplyingState())
            {
                anchor.valid = false;   // a restored / loaded position: the modules came with it
                continue;
            }
            move ((dsp::MacroId) i, from, now);
        }
    }

    void MacroMapper::move (dsp::MacroId id, float from, float to)
    {
        auto routing = hooks.getRouting();
        const auto lawsNow = hooks.getLaws();
        const auto current = hooks.readSettings();

        // Continue from where this slider's move began while nobody else touched what it writes
        const bool continues = anchor.valid && anchor.id == id && anchor.written == captureWritten()
                               && anchor.routing == routing.toString() && anchor.laws == lawsNow.toString();
        if (! continues)
            anchor = { true, id, from, current, lawsNow, {}, {}, {} };

        auto target = dsp::applyMacroMove (anchor.base, id, anchor.from, to);

        // SPACE keeps its sends (spec Q2): a delay / reverb this move switches on that a fresh chain holds
        // as an unused insert on the true law becomes a send - before its Mix rises (no routing dip)
        bool routingChanged = false;
        for (auto m : { dsp::ModuleId::delay, dsp::ModuleId::reverb })
        {
            const bool on = m == dsp::ModuleId::delay ? target.delayOn : target.reverbOn;
            const bool wasOn = m == dsp::ModuleId::delay ? current.delayOn : current.reverbOn;
            if (on && ! wasOn && routing.isInsert (m, 0) && ! lawsNow.isLegacy (m, 0))
                routingChanged = routing.setInsert (m, 0, false) || routingChanged;
        }
        if (routingChanged)
            hooks.setRouting (routing);

        // The dry of additive inserts stays where the module's own Mix put it (as with the 0.6.0 macros)
        auto laws = anchor.baseLaws;
        macrobake::keepDry (anchor.base, target, routing, laws);
        if (laws != lawsNow)
            hooks.setLaws (laws);

        const auto access = openGestures > 0 ? eqbands::accessFor (apvts) : plainAccess (apvts);
        macrobake::writeMacroFields (access, current, target);

        anchor.written = captureWritten();
        anchor.routing = hooks.getRouting().toString();
        anchor.laws = hooks.getLaws().toString();
    }
}
