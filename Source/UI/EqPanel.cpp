#include "EqPanel.h"
#include "HelpContent.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    using dsp::EqBandType;
    using dsp::ModuleId;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
    }

    //==========================================================================
    namespace eqedit
    {
        namespace
        {
            eqbands::Access access (VelvoxAudioProcessor& proc) { return eqbands::accessFor (proc.getState()); }

            // Runs `fn` as one undo step; a macro-only band gets its parameters first (same step)
            template <typename Fn>
            void edit (VelvoxAudioProcessor& proc, int instance, int band, const juce::String& name, Fn&& fn)
            {
                if (band < 0 || band >= dsp::maxEqBands)
                    return;
                proc.getUndo().perform (name, [&]
                {
                    materialise (proc, instance, band);
                    fn (access (proc));
                });
            }
        }

        juce::String paramId (int instance, int band, const char* field) { return eqbands::bandParamId (instance, band, field); }

        dsp::EqBand parameterBand (VelvoxAudioProcessor& proc, int instance, int band)
        {
            return eqbands::readBand (access (proc), instance, band);
        }

        ShownBands shownBands (VelvoxAudioProcessor& proc, int instance)
        {
            ShownBands out;
            const auto eff = proc.readEffectiveSettings().instance (instance).eq;
            const auto a = access (proc);
            for (int i = 0; i < dsp::maxEqBands; ++i)
            {
                auto& s = out[(size_t) i];
                const auto param = eqbands::readBand (a, instance, i);
                s.band = eff.bands[(size_t) i];
                s.used = ! s.band.isFree();
                s.macroOnly = s.used && param.isFree();
                s.gainOffset = s.band.gain - (s.macroOnly ? 0.0f : param.gain);
            }
            return out;
        }

        bool materialise (VelvoxAudioProcessor& proc, int instance, int band)
        {
            if (band < 0 || band >= dsp::maxEqBands)
                return false;
            const auto s = shownBands (proc, instance)[(size_t) band];
            if (! s.macroOnly)
                return false;
            auto b = s.band;
            b.gain = 0.0f;   // the macro adds its share on top (Macros.cpp, roleBand)
            b.on = true;
            eqbands::writeBand (access (proc), instance, band, b);
            return true;
        }

        int addBand (VelvoxAudioProcessor& proc, int instance, float freqHz, float gainDb)
        {
            // A slot free in the parameters AND in the effective settings, so a macro band keeps its place
            const auto shown = shownBands (proc, instance);
            const auto a = access (proc);
            int slot = -1;
            for (int i = 0; i < dsp::maxEqBands && slot < 0; ++i)
                if (! shown[(size_t) i].used && eqbands::readBand (a, instance, i).isFree())
                    slot = i;
            if (slot < 0)
                slot = eqbands::findFreeBand (a, instance);
            if (slot < 0)
                return -1;

            dsp::EqBand b;
            b.on = true;
            b.type = (int) EqBandType::bell;
            b.freq = juce::jlimit (dsp::eqdesign::minFreq, dsp::eqdesign::maxFreq, freqHz);
            b.gain = juce::jlimit (-dsp::eqdesign::maxGainDb, dsp::eqdesign::maxGainDb, gainDb);
            b.q = 1.0f;
            proc.getUndo().perform (de ("EQ-Band hinzufügen"), [&] { eqbands::writeBand (a, instance, slot, b); });
            return slot;
        }

        void deleteBand (VelvoxAudioProcessor& proc, int instance, int band)
        {
            if (band < 0 || band >= dsp::maxEqBands)
                return;
            proc.getUndo().perform (de ("EQ-Band löschen"), [&] { eqbands::writeBand (access (proc), instance, band, dsp::EqBand {}); });
        }

        void setType (VelvoxAudioProcessor& proc, int instance, int band, EqBandType type)
        {
            edit (proc, instance, band, "EQ-Band: Typ", [&] (const eqbands::Access& a)
            {
                a.set (paramId (instance, band, pid::eqband::type), (float) type);
            });
        }

        void setSlope (VelvoxAudioProcessor& proc, int instance, int band, int slopeIndex)
        {
            edit (proc, instance, band, "EQ-Band: Steilheit", [&] (const eqbands::Access& a)
            {
                a.set (paramId (instance, band, pid::eqband::slope), (float) juce::jlimit (0, dsp::numEqSlopes - 1, slopeIndex));
            });
        }

        void toggleOn (VelvoxAudioProcessor& proc, int instance, int band)
        {
            edit (proc, instance, band, "EQ-Band ein/aus", [&] (const eqbands::Access& a)
            {
                const auto id = paramId (instance, band, pid::eqband::on);
                a.set (id, a.get (id) > 0.5f ? 0.0f : 1.0f);
            });
        }

        void resetBand (VelvoxAudioProcessor& proc, int instance, int band)
        {
            edit (proc, instance, band, de ("EQ-Band zurücksetzen"), [&] (const eqbands::Access& a)
            {
                const dsp::EqBand defaults;
                a.set (paramId (instance, band, pid::eqband::gain), defaults.gain);
                a.set (paramId (instance, band, pid::eqband::q), defaults.q);
                a.set (paramId (instance, band, pid::eqband::slope), (float) defaults.slope);
            });
        }

        juce::String typeName (int type)
        {
            switch ((EqBandType) juce::jlimit (0, dsp::numEqBandTypes - 1, type))
            {
                case EqBandType::bell:      return "Bell";
                case EqBandType::lowShelf:  return "Low Shelf";
                case EqBandType::highShelf: return "High Shelf";
                case EqBandType::lowCut:    return "Low Cut";
                case EqBandType::highCut:   return "High Cut";
                case EqBandType::notch:     return "Notch";
                case EqBandType::bandPass:  return "Band Pass";
                case EqBandType::tilt:      return "Tilt";
            }
            return "Bell";
        }

        juce::String roleTag (int role)
        {
            switch ((dsp::EqRole) juce::jlimit (0, dsp::numEqRoles - 1, role))
            {
                case dsp::EqRole::none:     return {};
                case dsp::EqRole::presence: return "PRES";
                case dsp::EqRole::warmth:   return "WARM";
                case dsp::EqRole::air:      return "AIR";
                case dsp::EqRole::lowCut:   return "LC";
                case dsp::EqRole::mud:      return "MUD";
                case dsp::EqRole::autoMix:  return "AUTO";
                case dsp::EqRole::highCut:  return "HC";
            }
            return {};
        }

        bool hasGain (int type) noexcept
        {
            const auto t = (EqBandType) type;
            return t == EqBandType::bell || t == EqBandType::lowShelf || t == EqBandType::highShelf || t == EqBandType::tilt;
        }

        bool hasQ (int type) noexcept
        {
            const auto t = (EqBandType) type;
            return t == EqBandType::bell || t == EqBandType::lowShelf || t == EqBandType::highShelf || t == EqBandType::notch
                || t == EqBandType::bandPass;
        }

        juce::Colour bandColour (int band)
        {
            // 16 distinct hues, starting warm so band 1 differs from the teal sum curve
            static const juce::uint32 palette[dsp::maxEqBands] {
                0xfff59e0b, 0xfff472b6, 0xffa78bfa, 0xff60a5fa, 0xff34d399, 0xfffacc15, 0xfffb7185, 0xffc084fc,
                0xff38bdf8, 0xffa3e635, 0xfffb923c, 0xffe879f9, 0xff818cf8, 0xff22d3ee, 0xff4ade80, 0xfffda4af };
            return juce::Colour (palette[(size_t) juce::jlimit (0, dsp::maxEqBands - 1, band)]);
        }
    }
}
