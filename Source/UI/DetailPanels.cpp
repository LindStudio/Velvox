#include "DetailPanels.h"
#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../Presets/ModulePresets.h"
#include "Icons.h"
#include "HelpContent.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        // Design: panel 1180 x 460, padding 22 (vertical) / 28 (horizontal)
        constexpr int headerHeight = 66;
        constexpr int padding = 28;
        constexpr int paddingY = 22;
        constexpr int captionH = 18;
        constexpr int sectionGap = 12;

        void emphasise (std::initializer_list<Knob*> knobs, Knob::Prominence p)
        {
            for (auto* k : knobs)
                k->setProminence (p);
        }
    }

    //==========================================================================
    DetailPanel::DetailPanel (VelvoxAudioProcessor& p, juce::String t, juce::String s, const char* onId)
        : proc (p), apvts (p.getState()), title (std::move (t)), subtitle (std::move (s)), instance (constructionInstance)
    {
        if (onId != nullptr)
            onParamId = pidFor (onId);
        if (onParamId.isNotEmpty())
        {
            power = std::make_unique<PowerButton> (apvts, onParamId.toRawUTF8());
            addAndMakeVisible (*power);
        }

        helpButton.setTooltip ("Help for this module: what it does, how to use it, good starting points");
        helpButton.onClick = [this] { if (onHelp) onHelp (moduleId == -2 ? -1 : moduleId); };
        addAndMakeVisible (helpButton);
    }

    void DetailPanel::setVisual (std::unique_ptr<ModuleView> view)
    {
        visual = std::move (view);
        if (visual != nullptr)
            addAndMakeVisible (*visual);
        resized();
    }

    juce::String DetailPanel::pidFor (const char* baseId) const
    {
        if (instance > 0)
        {
            const auto id = dsp::instanceParamId (baseId, instance);
            if (apvts.getParameter (id) != nullptr)
                return id;
        }
        return baseId;
    }

    void DetailPanel::setModule (int selection)
    {
        const int id = selection < 0 ? selection : (int) dsp::selectionModule (selection);
        moduleId = id;
        if (id >= 0)
        {
            // Every control of the panel glows in the module colour
            const auto hue = moduleColour ((ModuleId) id);
            if (power != nullptr)
                power->setHue (hue);
            for (auto* k : knobs)
                k->slider.setColour (juce::Slider::rotarySliderFillColourId, hue);
            for (auto* t : toggles)
                t->setColour (juce::ToggleButton::tickColourId, hue);
            for (auto* s : segmentGroups)
                s->setAccent (hue);

            const auto svg = moduleIconSvg ((ModuleId) id).replace ("#FFFFFF", "#" + hue.toDisplayString (false));
            const auto utf8 = svg.toUTF8();
            icon = juce::Drawable::createFromImageData (utf8.getAddress(), utf8.sizeInBytes() - 1);

            // Slot info: position in the chain and how it runs
            const auto& r = proc.getRouting();
            const int index = r.indexOf ((ModuleId) id, instance);
            if (index >= 0)
                slotInfo = "SLOT " + juce::String (index + 1) + juce::String::fromUTF8 (" \xc2\xb7 ")
                         + (r.isInsert ((ModuleId) id, instance) ? "INSERT" : "SEND");
        }
        // Several instances of this module in the chain: the title says which one
        if (id >= 0 && (instance > 0 || proc.getRouting().countOf ((ModuleId) id) > 1))
            title << " " << (instance + 1);
        if (id >= 0 && ! getModulePresets ((ModuleId) id).empty())
        {
            presetsButton.setTooltip ("Starting points for this module only - the rest of the chain stays as it is");
            presetsButton.onClick = [this] { showModulePresets(); };
            addAndMakeVisible (presetsButton);
        }
        resized();
        repaint();
    }

    void DetailPanel::showModulePresets()
    {
        const auto& presets = getModulePresets ((ModuleId) moduleId);
        juce::PopupMenu menu;
        menu.addSectionHeader (juce::String (dsp::moduleDisplayName ((ModuleId) moduleId)) + " presets");
        for (int i = 0; i < (int) presets.size(); ++i)
            menu.addItem (i + 1, presets[(size_t) i].name);

        juce::Component::SafePointer<DetailPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton).withMinimumWidth (180),
                            [safe] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                const auto& list = getModulePresets ((ModuleId) safe->moduleId);
                                if (result - 1 < (int) list.size())
                                    applyModulePreset (list[(size_t) result - 1], safe->apvts, safe->instance);
                            });
    }

    Knob& DetailPanel::knob (const char* paramId, const juce::String& label)
    {
        const auto mapped = pidFor (paramId);
        auto k = std::make_unique<Knob> (apvts, mapped.toRawUTF8(), label, moduleId >= 0 ? moduleColour ((ModuleId) moduleId) : colours::teal);
        // Macro markers belong to the first instance only (macros never move the others)
        if (instance == 0 && proc.effectiveValueOf (paramId).has_value())
        {
            const juce::String id (paramId);
            auto* procPtr = &proc;
            k->effectiveValue = [procPtr, id] { return procPtr->effectiveValueOf (id).value_or (0.0f); };
        }
        auto& ref = *k;
        knobs.push_back (k.get());
        addAndMakeVisible (ref);
        owned.push_back (std::move (k));
        return ref;
    }

    Toggle& DetailPanel::toggle (const char* paramId, const juce::String& label)
    {
        auto t = std::make_unique<Toggle> (apvts, pidFor (paramId).toRawUTF8(), label);
        auto& ref = *t;
        toggles.push_back (t.get());
        addAndMakeVisible (ref);
        owned.push_back (std::move (t));
        return ref;
    }

    Segments& DetailPanel::segments (const char* paramId, juce::StringArray labels, bool withOverflow)
    {
        const auto id = pidFor (paramId);
        if (labels.isEmpty())
        {
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
                labels = c->choices;
            else
                labels = { "Off", "On" };
        }
        auto s = std::make_unique<Segments> (apvts, id.toRawUTF8(), labels, colours::teal, withOverflow);
        auto& ref = *s;
        segmentGroups.push_back (s.get());
        addAndMakeVisible (ref);
        owned.push_back (std::move (s));
        return ref;
    }

    void DetailPanel::knobGrid (const std::vector<juce::Component*>& items, juce::Rectangle<int> area, int columns)
    {
        const int n = (int) items.size();
        if (n == 0 || columns <= 0)
            return;
        const int rows = (n + columns - 1) / columns;
        const int w = area.getWidth() / columns, h = area.getHeight() / rows;
        for (int i = 0; i < n; ++i)
            if (auto* c = items[(size_t) i])
                c->setBounds (area.getX() + (i % columns) * w, area.getY() + (i / columns) * h, w, h);
    }

    Choice& DetailPanel::choice (const char* paramId, const juce::String& caption)
    {
        auto c = std::make_unique<Choice> (apvts, pidFor (paramId).toRawUTF8(), caption);
        auto& ref = *c;
        addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    void DetailPanel::row (std::initializer_list<juce::Component*> items, juce::Rectangle<int> area)
    {
        const int n = (int) items.size();
        if (n == 0)
            return;
        const int w = area.getWidth() / n;
        int i = 0;
        for (auto* c : items)
        {
            if (c != nullptr)
                c->setBounds (area.getX() + i * w, area.getY(), w, area.getHeight());
            ++i;
        }
    }

    void DetailPanel::section (const juce::String& caption, juce::Rectangle<int> area)
    {
        sections.push_back ({ caption, area });
    }

    void DetailPanel::placeGroup (const juce::String& caption, juce::Component& c, juce::Rectangle<int>& area, int height)
    {
        auto group = area.removeFromTop (captionH + 4 + height);
        section (caption, group);
        c.setBounds (group.withTrimmedTop (captionH + 4));
        area.removeFromTop (12);
    }

    void DetailPanel::placeSwitches (std::initializer_list<juce::Component*> items, juce::Rectangle<int>& area)
    {
        auto line = area.removeFromTop (26);
        for (auto* c : items)
            if (c != nullptr)
            {
                const int w = 30 + juce::GlyphArrangement::getStringWidthInt (medium (12.0f), dynamic_cast<juce::Button*> (c) != nullptr
                                                                                                 ? dynamic_cast<juce::Button*> (c)->getButtonText() : juce::String()) + 22;
                c->setBounds (line.removeFromLeft (w));
            }
        area.removeFromTop (12);
    }

    void DetailPanel::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = onParamId.isEmpty() || apvts.getRawParameterValue (onParamId)->load() > 0.5f;
        const auto hue = moduleId >= 0 && moduleId < dsp::numModules ? moduleColour ((ModuleId) moduleId) : colours::teal;

        // Design panel: #07080D, border #161925, radius 12
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

        // Header: neon icon 30 + NAME (SemiBold 15, tracking 2.5) + slot info, functions below
        auto header = getLocalBounds().withTrimmedTop (paddingY - 6).removeFromTop (headerHeight - paddingY + 6).reduced (padding, 0);
        header.removeFromLeft (power != nullptr ? 44 : 0);
        if (icon != nullptr)
        {
            const auto iconArea = header.removeFromLeft (30).toFloat().withSizeKeepingCentre (30.0f, 30.0f).translated (0.0f, -6.0f);
            if (on)
                drawGlow (g, iconArea.getCentre(), 24.0f, 0.25f, hue);
            icon->drawWithin (g, iconArea, juce::RectanglePlacement::centred, on ? 1.0f : 0.4f);
            header.removeFromLeft (12);
        }
        header.removeFromRight (helpButton.getWidth() + 10);
        if (presetsButton.isVisible())
            header.removeFromRight (presetsButton.getWidth() + 12);
        auto nameLine = header.removeFromTop (header.getHeight() / 2 + 4);
        const auto shownTitle = title.toUpperCase();
        const auto titleFont = semiBold (15.0f, 2.5f / 15.0f);
        g.setColour (on ? colours::textStrong : colours::textMuted);
        g.setFont (titleFont);
        const int titleW = juce::GlyphArrangement::getStringWidthInt (titleFont, shownTitle) + 16;
        g.drawText (shownTitle, nameLine.removeFromLeft (titleW), juce::Justification::centredLeft, false);
        g.setColour (on ? colours::textFaint : colours::textFaint.withAlpha (0.6f));
        g.setFont (semiBold (10.0f, 0.12f));
        const auto stateText = on ? slotInfo : (slotInfo.isEmpty() ? juce::String ("OFF") : slotInfo + juce::String::fromUTF8 (" \xc2\xb7 ") + "OFF");
        g.drawText (stateText, nameLine, juce::Justification::centredLeft, false);
        g.setColour (colours::textMuted);
        g.setFont (font (11.0f / 0.93f));
        g.drawFittedText (subtitle, header, juce::Justification::topLeft, 1, 0.9f);

        // Visualizer box: #040509, border #13161E, radius 8
        if (! visualArea.isEmpty())
        {
            const auto v = visualArea.toFloat();
            g.setColour (colours::panelInset);
            g.fillRoundedRectangle (v, 8.0f);
            g.setColour (colours::borderInset);
            g.drawRoundedRectangle (v.reduced (0.5f), 8.0f, 1.0f);
        }

        // Section labels (RATE, MODE ...): Montserrat SemiBold 10, #5B6478
        for (const auto& s : sections)
        {
            g.setColour (colours::textFaint);
            g.setFont (semiBold (10.0f, 0.15f));
            g.drawText (s.caption.toUpperCase(), s.area.toFloat().withHeight ((float) captionH), juce::Justification::centredLeft, false);
        }
    }

    void DetailPanel::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).withTrimmedTop (paddingY - 6).reduced (padding, 0);
        if (power != nullptr)
            power->setBounds (header.removeFromLeft (32).withSizeKeepingCentre (32, 32).translated (0, -4));
        helpButton.setBounds (header.removeFromRight (32).withSizeKeepingCentre (32, 30).translated (0, -4));
        header.removeFromRight (10);
        presetsButton.setBounds (header.removeFromRight (96).withSizeKeepingCentre (96, 30).translated (0, -4));

        auto content = r.reduced (padding, 0).withTrimmedBottom (paddingY);
        visualArea = {};
        sideArea = {};
        if (visual != nullptr && visualHeight > 0)
        {
            auto band = content.removeFromTop (visualHeight);
            content.removeFromTop (14);
            if (hasSideControls)
            {
                visualArea = band.removeFromLeft (juce::roundToInt ((float) band.getWidth() * visualShare));
                band.removeFromLeft (24);
                sideArea = band;
            }
            else
            {
                visualArea = band;
            }
            visual->setBounds (visualArea.reduced (1));
        }
        else if (hasSideControls)
        {
            sideArea = content.removeFromTop (visualHeight);
            content.removeFromTop (14);
        }

        sections.clear();
        layoutContent (content);
    }

    void DetailPanel::refresh()
    {
        for (auto* k : knobs)
            k->refreshEffective();
        if (visual != nullptr && live != nullptr)
            visual->update (*live);

        const int on = onParamId.isEmpty() || apvts.getRawParameterValue (onParamId)->load() > 0.5f ? 1 : 0;
        if (on != lastOn)
        {
            lastOn = on;
            for (auto* c : getChildren())
                if (c != power.get() && c != visual.get() && c != &presetsButton && c != &helpButton)
                    c->setAlpha (on ? 1.0f : 0.55f);
            repaint();
        }
    }

    //==========================================================================
    namespace
    {
        // Inner area of a section (below its caption)
        juce::Rectangle<int> inner (juce::Rectangle<int> section) { return section.withTrimmedTop (captionH).reduced (10, 4); }

        juce::Label& noteLabel (juce::Label& l)
        {
            l.setFont (font (13.5f));
            l.setColour (juce::Label::textColourId, colours::textDim);
            l.setJustificationType (juce::Justification::centredLeft);
            return l;
        }

        // Two stacked sections filling the area; the top one takes `topShare`
        // (more room for the group holding the main controls)
        std::pair<juce::Rectangle<int>, juce::Rectangle<int>> split (juce::Rectangle<int> r, float topShare = 0.5f)
        {
            auto top = r.removeFromTop (juce::roundToInt ((float) (r.getHeight() - sectionGap) * topShare));
            r.removeFromTop (sectionGap);
            return { top, r };
        }

        // Three stacked sections (top, middle, bottom) with the given shares
        std::array<juce::Rectangle<int>, 3> split3 (juce::Rectangle<int> r, float topShare = 0.34f, float midShare = 0.33f)
        {
            const int total = r.getHeight() - 2 * sectionGap;
            auto top = r.removeFromTop (juce::roundToInt ((float) total * topShare));
            r.removeFromTop (sectionGap);
            auto mid = r.removeFromTop (juce::roundToInt ((float) total * midShare));
            r.removeFromTop (sectionGap);
            return { top, mid, r };
        }

        float grFrom (const LiveData* live, const LiveData::History LiveData::* h) { return live != nullptr ? (live->*h).ago (0) : 0.0f; }

        // EQ: curve top left; cuts (switch + slope) and band types at the side; band knobs below
        class EqPanel final : public DetailPanel
        {
        public:
            explicit EqPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Equalizer", help::shortDescription ((int) ModuleId::eq), pid::eqOn)
            {
                visualHeight = 170;
                hasSideControls = true;
                visualShare = 0.6f;
                lcOn = &toggle (pid::hpfOn, "On");
                lcSlope = &segments (pid::hpfSlope);
                lowType = &segments (pid::lowType);
                hcOn = &toggle (pid::lpfOn, "On");
                hcSlope = &segments (pid::lpfSlope);
                highType = &segments (pid::highType);
                lcFreq = &knob (pid::hpfFreq, "Low Cut");
                lowFreq = &knob (pid::lowFreq, "Low Freq");
                lowGain = &knob (pid::lowGain, "Low Gain");
                lowQ = &knob (pid::lowQ, "Low Q");
                midFreq = &knob (pid::midFreq, "Mid Freq");
                midGain = &knob (pid::midGain, "Mid Gain");
                midQ = &knob (pid::midQ, "Mid Q");
                highFreq = &knob (pid::highFreq, "High Freq");
                highGain = &knob (pid::highGain, "High Gain");
                highQ = &knob (pid::highQ, "High Q");
                hcFreq = &knob (pid::lpfFreq, "High Cut");
                mix = &knob (pid::eqMix, "Mix");
                emphasise ({ lowGain, midGain, highGain }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                auto left = side.removeFromLeft (side.getWidth() / 2 - 8);
                side.removeFromLeft (16);
                auto right = side;
                const auto cut = [&] (const juce::String& caption, Toggle& on, Segments& slope, juce::Rectangle<int>& col)
                {
                    auto group = col.removeFromTop (captionH + 4 + 30);
                    section (caption, group);
                    auto line = group.withTrimmedTop (captionH + 4);
                    on.setBounds (line.removeFromLeft (64));
                    slope.setBounds (line);
                    col.removeFromTop (12);
                };
                cut ("Low Cut", *lcOn, *lcSlope, left);
                placeGroup ("Low Band", *lowType, left);
                cut ("High Cut", *hcOn, *hcSlope, right);
                placeGroup ("High Band", *highType, right);
                knobGrid ({ lcFreq, lowFreq, lowGain, lowQ, midFreq, midGain, midQ, highFreq, highGain, highQ, hcFreq, mix }, r, 6);
            }

            Toggle *lcOn, *hcOn;
            Segments *lcSlope, *lowType, *hcSlope, *highType;
            Knob *lcFreq, *lowFreq, *lowGain, *lowQ, *midFreq, *midGain, *midQ, *highFreq, *highGain, *highQ, *hcFreq, *mix;
        };

        // COMPRESSOR: transfer view, detector + GR at the side, all dynamics knobs in one row
        class CompPanel final : public DetailPanel
        {
        public:
            explicit CompPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Compressor", help::shortDescription ((int) ModuleId::comp), pid::compOn)
            {
                hasSideControls = true;
                for (auto& [id, label, ptr] : std::initializer_list<std::tuple<const char*, const char*, Knob**>> {
                         { pid::compThreshold, "Threshold", &threshold }, { pid::compRatio, "Ratio", &ratio },
                         { pid::compAttack, "Attack", &attack }, { pid::compRelease, "Release", &release },
                         { pid::compKnee, "Knee", &knee }, { pid::compMakeup, "Makeup", &makeup },
                         { pid::compScHpf, "SC Filter", &scHpf }, { pid::compMix, "Mix", &mix } })
                    *ptr = &knob (id, label);
                detector = &segments (pid::compDetector);
                emphasise ({ threshold, ratio }, Knob::Prominence::primary);
                addAndMakeVisible (gr);
                gr.setTooltip ("Gain reduction in dB");
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::compGr));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                gr.setBounds (side.removeFromRight (44));
                side.removeFromRight (16);
                placeGroup ("Detector", *detector, side);
                knobGrid ({ threshold, ratio, attack, release, knee, makeup, scHpf, mix }, r, 8);
            }

            Knob *threshold, *ratio, *attack, *release, *knee, *makeup, *mix, *scHpf;
            Segments* detector;
            GainReductionMeter gr { colours::accent, "GR" };
        };

        // DE-ESSER: mode and listen at the side with the GR meter, detection knobs below
        class DeEssPanel final : public DetailPanel
        {
        public:
            explicit DeEssPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "De-esser", help::shortDescription ((int) ModuleId::deess), pid::deessOn)
            {
                hasSideControls = true;
                mode = &segments (pid::deessMode);
                listen = &toggle (pid::deessListen, "Listen (nur S-Laute)");
                freq = &knob (pid::deessFreq, "Frequency");
                threshold = &knob (pid::deessThreshold, "Threshold");
                range = &knob (pid::deessRange, "Range");
                emphasise ({ freq, threshold }, Knob::Prominence::primary);
                addAndMakeVisible (gr);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::deessGr));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                gr.setBounds (side.removeFromRight (44));
                side.removeFromRight (16);
                placeGroup ("Mode", *mode, side);
                placeSwitches ({ listen }, side);
                knobGrid ({ freq, threshold, range, nullptr, nullptr, nullptr }, r, 6);
            }

            Knob *freq, *threshold, *range;
            Segments* mode;
            Toggle* listen;
            GainReductionMeter gr { colours::accent, "GR" };
        };

        // SATURATION: character as segments, drive / output / mix
        class SatPanel final : public DetailPanel
        {
        public:
            explicit SatPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Saturation", help::shortDescription ((int) ModuleId::sat), pid::satOn)
            {
                visualHeight = 170;
                hasSideControls = true;
                character = &segments (pid::satCharacter);
                drive = &knob (pid::satDrive, "Drive");
                output = &knob (pid::satOutput, "Output");
                mix = &knob (pid::satMix, "Mix");
                drive->setProminence (Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Charakter", *character, side, 34);
                knobGrid ({ drive, output, mix, nullptr, nullptr, nullptr }, r, 6);
            }

            Segments* character;
            Knob *drive, *output, *mix;
        };

        // DOUBLER: voices, stereo and tone in one knob row
        class DoublerPanel final : public DetailPanel
        {
        public:
            explicit DoublerPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Doubler", help::shortDescription ((int) ModuleId::doubler), pid::doublerOn)
            {
                width = &knob (pid::doublerWidth, "Width");
                delay = &knob (pid::doublerDelay, "Timing");
                detune = &knob (pid::doublerDetune, "Detune");
                monoSafe = &knob (pid::doublerMonoSafe, "Mono Safe");
                tone = &knob (pid::doublerTone, "Tone");
                mix = &knob (pid::doublerMix, "Mix");
                emphasise ({ mix, width }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                knobGrid ({ width, delay, detune, monoSafe, tone, mix }, r, 6);
            }

            Knob *mix, *width, *delay, *detune, *monoSafe, *tone;
        };

        // PITCH: keep-formants switch and latency note at the side, transpose knobs below
        class PitchPanel final : public DetailPanel
        {
        public:
            explicit PitchPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pitch", help::shortDescription ((int) ModuleId::pitch), pid::pitchOn)
            {
                hasSideControls = true;
                keep = &toggle (pid::pitchPreserveFormants, "Formanten erhalten");
                semis = &knob (pid::pitchSemitones, "Pitch");
                fine = &knob (pid::pitchCents, "Fine");
                formant = &knob (pid::pitchFormant, "Formant");
                mix = &knob (pid::pitchMix, "Mix");
                emphasise ({ semis }, Knob::Prominence::primary);
                info.setMinimumHorizontalScale (0.9f);
                addAndMakeVisible (noteLabel (info));
            }

            void refresh() override
            {
                DetailPanel::refresh();
                auto withPitch = proc.getRouting();
                if (! withPitch.contains (dsp::ModuleId::pitch))
                    withPitch.insertDefault (dsp::ModuleId::pitch, 0);
                const int latency = proc.getChain().getLatencyFor (withPitch, true) - proc.getChain().getLatencyFor (withPitch, false);
                const double ms = 1000.0 * latency / std::max (1.0, proc.getSampleRate());
                const juce::String text = "While on, pitch adds " + juce::String (juce::roundToInt (ms)) + " ms latency. "
                                          "FL Studio compensates it automatically; avoid automating the on/off switch.";
                if (info.getText() != text)
                    info.setText (text, juce::dontSendNotification);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeSwitches ({ keep }, side);
                info.setJustificationType (juce::Justification::topLeft);
                info.setBounds (side.removeFromTop (60));
                knobGrid ({ semis, fine, formant, mix, nullptr, nullptr }, r, 6);
            }

            Knob *semis, *fine, *formant, *mix;
            Toggle* keep;
            juce::Label info;
        };

        // STUTTER (chop): pattern grid, rate and mode as segments; gate and smoothing
        class ChopPanel final : public DetailPanel
        {
        public:
            explicit ChopPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Stutter", help::shortDescription ((int) ModuleId::chop), pid::chopOn)
            {
                visualHeight = 200;
                hasSideControls = true;
                rate = &segments (pid::chopRate);
                pattern = &choice (pid::chopPattern, {});
                depth = &knob (pid::chopDepth, "Gate");
                smooth = &knob (pid::chopSmooth, "Smooth");
                depth->setProminence (Knob::Prominence::primary);
                info.setText ("Steps follow the song position while FL Studio plays. R = repeat the previous slice (stutter).", juce::dontSendNotification);
                addAndMakeVisible (noteLabel (info));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Rate", *rate, side);
                placeGroup ("Pattern", *pattern, side, 32);
                info.setBounds (r.removeFromBottom (20));
                knobGrid ({ depth, smooth, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }, r, 8);
            }

            Segments* rate;
            Choice* pattern;
            Knob *depth, *smooth;
            juce::Label info;
        };

        // DELAY: time divisions as segments, switches, echo + colour + ducking knobs
        class DelayPanel final : public DetailPanel
        {
        public:
            explicit DelayPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Delay", help::shortDescription ((int) ModuleId::delay), pid::delayOn)
            {
                hasSideControls = true;
                division = &segments (pid::delayDivision, { "1/4", "1/4D", "1/8", "1/8D", "1/8T", "1/16" }, true);
                source = &segments (pid::duckSource, { "Vocal", "Sidechain" });
                sync = &toggle (pid::delaySync, "Tempo-Sync");
                pingPong = &toggle (pid::delayPingPong, "Ping-Pong");
                time = &knob (pid::delayTime, "Time");
                feedback = &knob (pid::delayFeedback, "Feedback");
                spread = &knob (pid::delaySpread, "Offset");
                lowCut = &knob (pid::delayLowCut, "Low Cut");
                highCut = &knob (pid::delayHighCut, "High Cut");
                modulation = &knob (pid::delayMod, "Wobble");
                drive = &knob (pid::delayDrive, "Drive");
                diffusion = &knob (pid::delayDiffusion, "Diffusion");
                toReverb = &knob (pid::delayToReverb, "Into Reverb");
                mix = &knob (pid::delayMix, "Mix");
                duckAmount = &knob (pid::delayDuckAmount, "Ducking");
                duckThreshold = &knob (pid::delayDuckThreshold, "Duck Thresh");
                duckAttack = &knob (pid::delayDuckAttack, "Duck Attack");
                duckRelease = &knob (pid::delayDuckRelease, "Duck Release");
                emphasise ({ feedback, mix }, Knob::Prominence::primary);
                addAndMakeVisible (gr);
                addAndMakeVisible (noteLabel (info));
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::delayDuck));
                const bool synced = apvts.getRawParameterValue (pidFor (pid::delaySync))->load() > 0.5f;
                time->setEnabled (! synced);
                division->setEnabled (synced);

                const auto& m = proc.getChain().meters;
                juce::String text = synced ? (m.tempoSynced.load() ? juce::String (juce::roundToInt (m.effectiveDelayMs.load())) + " ms at host tempo"
                                                                  : "No host tempo: using the Time value")
                                           : juce::String();
                if (apvts.getRawParameterValue (pid::duckSource)->load() > 0.5f && ! m.sidechainInUse.load())
                    text << (text.isEmpty() ? "" : "  |  ") << "No sidechain connected: ducking follows the vocal";
                if (info.getText() != text)
                    info.setText (text, juce::dontSendNotification);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Time", *division, side);
                placeSwitches ({ sync, pingPong }, side);
                gr.setBounds (side.removeFromRight (44));
                side.removeFromRight (12);
                placeGroup ("Ducking source", *source, side);

                // Tempo / sidechain status under the knobs (the side area is full)
                info.setBounds (r.removeFromBottom (18));
                knobGrid ({ time, feedback, spread, lowCut, highCut, modulation, drive, diffusion, toReverb,
                            mix, duckAmount, duckThreshold, duckAttack, duckRelease }, r, 7);
            }

            Segments *division, *source;
            Toggle *sync, *pingPong;
            Knob *time, *feedback, *spread, *lowCut, *highCut, *modulation, *drive, *diffusion, *toReverb, *mix;
            Knob *duckAmount, *duckThreshold, *duckAttack, *duckRelease;
            GainReductionMeter gr { colours::accent, "Duck" };
            juce::Label info;
        };

        // REVERB: room list (characters), tail and ducking knobs
        class ReverbPanel final : public DetailPanel
        {
        public:
            explicit ReverbPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Reverb", help::shortDescription ((int) ModuleId::reverb), pid::reverbOn)
            {
                hasSideControls = true;
                type = &segments (pid::reverbType);
                source = &segments (pid::duckSource, { "Vocal", "Sidechain" });
                size = &knob (pid::reverbSize, "Size");
                decay = &knob (pid::reverbDecay, "Decay");
                preDelay = &knob (pid::reverbPreDelay, "Pre-Delay");
                tone = &knob (pid::reverbDamping, "Damping");
                width = &knob (pid::reverbWidth, "Width");
                lowCut = &knob (pid::reverbLowCut, "Low Cut");
                highCut = &knob (pid::reverbHighCut, "High Cut");
                movement = &knob (pid::reverbMod, "Modulation");
                early = &knob (pid::reverbEarly, "Early");
                diffusion = &knob (pid::reverbDiffusion, "Diffusion");
                bass = &knob (pid::reverbBass, "Bass Decay");
                mix = &knob (pid::reverbMix, "Mix");
                duckAmount = &knob (pid::reverbDuckAmount, "Ducking");
                duckThreshold = &knob (pid::reverbDuckThreshold, "Duck Thresh");
                duckAttack = &knob (pid::reverbDuckAttack, "Duck Attack");
                duckRelease = &knob (pid::reverbDuckRelease, "Duck Release");
                emphasise ({ decay, mix }, Knob::Prominence::primary);
                addAndMakeVisible (gr);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::reverbDuck));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Room", *type, side, 34);
                gr.setBounds (side.removeFromRight (44));
                side.removeFromRight (12);
                placeGroup ("Ducking source", *source, side);
                knobGrid ({ size, decay, preDelay, tone, width, lowCut, highCut, movement,
                            early, diffusion, bass, mix, duckAmount, duckThreshold, duckAttack, duckRelease }, r, 8);
            }

            Segments *type, *source;
            Knob *size, *decay, *preDelay, *tone, *width, *lowCut, *highCut, *movement, *early, *diffusion, *bass, *mix;
            Knob *duckAmount, *duckThreshold, *duckAttack, *duckRelease;
            GainReductionMeter gr { colours::accent, "Duck" };
        };

        // OUTPUT: stereo image + loudness view, level match at the side, gains, stereo and read-outs below
        class OutputPanel final : public DetailPanel
        {
        public:
            explicit OutputPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Output", help::shortDescription (-1), nullptr)
            {
                hasSideControls = true;
                mode = &segments (pid::levelMatchMode);
                window = &segments (pid::levelMatchWindow);
                clip = &toggle (pid::safetyClip, "Clip Guard");
                inGain = &knob (pid::inputGain, "Input Gain");
                width = &knob (pid::stereoWidth, "Width");
                monoBelow = &knob (pid::monoBelow, "Mono Below");
                range = &knob (pid::levelMatchRange, "Max Correction");
                emphasise ({ width }, Knob::Prominence::primary);
                addAndMakeVisible (correlation);
                correlation.setTooltip ("Phase correlation: +1 = mono-safe, below 0 = parts cancel in mono");
                for (auto* l : { &inValue, &procValue, &matchValue })
                {
                    l->setFont (monoFont (22.0f));
                    l->setColour (juce::Label::textColourId, colours::text);
                    l->setJustificationType (juce::Justification::centred);
                    addAndMakeVisible (*l);
                }
            }

            void refresh() override
            {
                DetailPanel::refresh();
                const auto& m = proc.getChain().meters;
                correlation.push (m.correlation.load());
                const auto fmt = [] (float lufs) { return lufs <= -99.0f ? juce::String ("-") : juce::String (lufs, 1); };
                const auto set = [] (juce::Label& l, const juce::String& t) { if (l.getText() != t) l.setText (t, juce::dontSendNotification); };
                set (inValue, fmt (m.inputLufs.load()));
                set (procValue, fmt (m.processedLufs.load()));
                const float match = m.levelMatchDb.load();
                set (matchValue, (match > 0.05f ? "+" : "") + juce::String (match, 1));
            }

            void paintOverChildren (juce::Graphics& g) override
            {
                const auto under = [&] (juce::Label& l, const juce::String& text)
                {
                    g.setColour (colours::knobName);
                    g.setFont (semiBold (9.0f, 0.06f));
                    g.drawText (text.toUpperCase(), l.getBounds().toFloat().translated (0.0f, 22.0f).withHeight (14.0f), juce::Justification::centred, false);
                };
                under (inValue, "Input LUFS");
                under (procValue, "Processed LUFS");
                under (matchValue, "Match dB");
                g.setColour (colours::knobName);
                g.setFont (semiBold (9.0f, 0.06f));
                g.drawText ("CORRELATION", correlation.getBounds().toFloat().translated (0.0f, 30.0f).withHeight (14.0f), juce::Justification::centred, false);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Level Match", *mode, side);
                placeGroup ("Measure over", *window, side);
                placeSwitches ({ clip }, side);

                knobGrid ({ inGain, width, monoBelow, range, &inValue, &procValue, &matchValue, &correlation }, r, 8);
                for (auto* l : { &inValue, &procValue, &matchValue })
                    l->setBounds (l->getBounds().withSizeKeepingCentre (l->getWidth(), 28));
                correlation.setBounds (correlation.getBounds().withSizeKeepingCentre (correlation.getWidth() - 16, 28));
            }

            Knob *inGain, *width, *monoBelow, *range;
            Segments *mode, *window;
            Toggle* clip;
            CorrelationView correlation;
            juce::Label inValue, procValue, matchValue;
        };

        //======================================================================
        // PULSE: rate and curve as segments, depth / shape / smoothing knobs
        class PulsePanel final : public DetailPanel
        {
        public:
            explicit PulsePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pulse", help::shortDescription ((int) ModuleId::pulse), pid::pulseOn)
            {
                hasSideControls = true;
                rate = &segments (pid::pulseRate, pulseRateLabels (p), true);
                interp = &segments (pid::pulseInterp);
                depth = &knob (pid::pulseDepth, "Depth");
                tension = &knob (pid::pulseTension, "Shape");
                smooth = &knob (pid::pulseSmooth, "Smooth");
                attack = &knob (pid::pulseAttack, "Attack");
                release = &knob (pid::pulseRelease, "Release");
                bpm = &knob (pid::pulseBpm, "Manual BPM");
                mix = &knob (pid::pulseMix, "Mix");
                emphasise ({ depth }, Knob::Prominence::primary);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                // The manual tempo only matters when the host sends none
                const bool host = proc.getChain().getPulse().displayHostTempo.load();
                bpm->setAlpha (host ? 0.4f : (apvts.getRawParameterValue (pid::pulseOn)->load() > 0.5f ? 1.0f : 0.55f));
            }

        private:
            // The design's five rates, mapped onto the parameter's names where they exist
            static juce::StringArray pulseRateLabels (VelvoxAudioProcessor& p)
            {
                juce::StringArray out;
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.getState().getParameter (pid::pulseRate)))
                    for (auto* name : { "1/2", "1/4", "1/8", "1/16", "1/32" })
                        if (c->choices.contains (name))
                            out.add (name);
                return out;
            }

            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Rate", *rate, side);
                placeGroup ("Curve", *interp, side);
                knobGrid ({ depth, tension, smooth, attack, release, bpm, mix }, r, 7);
            }

            Segments *rate, *interp;
            Knob *depth, *tension, *smooth, *attack, *release, *bpm, *mix;
        };

        // VOCAL SMASH: big SMASH knob (150 px) with the GR meter, then the squash path
        class SmashPanel final : public DetailPanel
        {
        public:
            explicit SmashPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Smash", help::shortDescription ((int) ModuleId::smash), pid::smashOn)
            {
                hasSideControls = true;
                visualShare = 0.62f;
                amount = &knob (pid::smashAmount, "Smash");
                attack = &knob (pid::smashAttack, "Attack");
                release = &knob (pid::smashRelease, "Release");
                tone = &knob (pid::smashTone, "Tone");
                output = &knob (pid::smashOutput, "Output");
                blend = &knob (pid::smashBlend, "Mix");
                amount->setDialSize (dialsize::smash);
                emphasise ({ blend }, Knob::Prominence::primary);
                addAndMakeVisible (gr);
                gr.setTooltip ("Gain reduction of the squashed path");
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::smashGr));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                // The big knob needs more than the visualizer band: it spans band + knob row
                auto side = sideArea.withBottom (r.getBottom());
                gr.setBounds (side.removeFromRight (44).withTrimmedBottom (20));
                side.removeFromRight (16);
                amount->setBounds (side.withSizeKeepingCentre (side.getWidth(), std::min (side.getHeight(), dialsize::smash + 40)));
                r.setRight (sideArea.getX() - 24);
                knobGrid ({ attack, release, tone, output, blend }, r, 5);
            }

            Knob *amount, *attack, *release, *tone, *output, *blend;
            GainReductionMeter gr { colours::accent, "GR" };
        };

        // CHOIR: voices as segments, then detune / timing / width / blend
        class ChoirPanel final : public DetailPanel
        {
        public:
            explicit ChoirPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Choir", help::shortDescription ((int) ModuleId::choir), pid::choirOn)
            {
                hasSideControls = true;
                voices = &segments (pid::choirVoices);
                detune = &knob (pid::choirDetune, "Detune");
                timing = &knob (pid::choirTiming, "Timing");
                humanize = &knob (pid::choirHumanize, "Humanize");
                width = &knob (pid::choirWidth, "Width");
                density = &knob (pid::choirDensity, "Density");
                mix = &knob (pid::choirMix, "Mix");
                emphasise ({ detune, mix }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Stimmen", *voices, side);
                knobGrid ({ detune, timing, humanize, width, density, mix }, r, 6);
            }

            Segments* voices;
            Knob *detune, *timing, *humanize, *width, *density, *mix;
        };

        // SMOOTH: character as segments, Delta switch, detection and timing knobs
        class SmoothPanel final : public DetailPanel
        {
        public:
            explicit SmoothPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Smooth", help::shortDescription ((int) ModuleId::smooth), pid::smoothOn)
            {
                hasSideControls = true;
                mode = &segments (pid::smoothMode);
                delta = &toggle (pid::smoothDelta, "Delta (nur Entferntes)");
                amount = &knob (pid::smoothAmount, "Amount");
                sensitivity = &knob (pid::smoothSensitivity, "Sharpness");
                low = &knob (pid::smoothLow, "Focus Low");
                high = &knob (pid::smoothHigh, "Focus High");
                attack = &knob (pid::smoothAttack, "Speed");
                release = &knob (pid::smoothRelease, "Release");
                output = &knob (pid::smoothOutput, "Output");
                emphasise ({ amount }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto side = sideArea;
                placeGroup ("Charakter", *mode, side, 34);
                placeSwitches ({ delta }, side);
                knobGrid ({ amount, sensitivity, low, high, attack, release, output }, r, 7);
            }

            Segments* mode;
            Toggle* delta;
            Knob *amount, *sensitivity, *low, *high, *attack, *release, *output;
        };

        // LEVEL RIDE: rider view, target / range / speed / gate knobs
        class RidePanel final : public DetailPanel
        {
        public:
            explicit RidePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Level Ride", help::shortDescription ((int) ModuleId::ride), pid::rideOn)
            {
                target = &knob (pid::rideTarget, "Target");
                boost = &knob (pid::rideMaxBoost, "Max Boost");
                cut = &knob (pid::rideMaxCut, "Max Cut");
                response = &knob (pid::rideResponse, "Speed");
                amount = &knob (pid::rideAmount, "Amount");
                phrase = &knob (pid::ridePhrase, "Phrase");
                transient = &knob (pid::rideTransient, "Transients");
                silence = &knob (pid::rideSilence, "Gate Floor");
                emphasise ({ target }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                knobGrid ({ target, boost, cut, response, amount, phrase, transient, silence }, r, 8);
            }

            Knob *target, *boost, *cut, *response, *amount, *phrase, *transient, *silence;
        };

        //======================================================================
        // VOCAL CHECK results: measured facts, what the last automatic mix changed, and the
        // diagnoses (issue, evidence, confidence, why it matters, suggestion, whether the
        // chain already takes care of it). Scrolls when it does not fit.
        class FindingsList final : public juce::Component
        {
        public:
            explicit FindingsList (VelvoxAudioProcessor& p) : proc (p), content (*this)
            {
                viewport.setViewedComponent (&content, false);
                viewport.setScrollBarsShown (true, false);
                viewport.setScrollBarThickness (8);
                addAndMakeVisible (viewport);
            }

            std::function<void (int moduleId)> onOpen;

            void resized() override
            {
                viewport.setBounds (getLocalBounds());
                content.layout (getWidth() - 10);
            }

            void refresh (bool moduleOn)
            {
                const auto& r = proc.getLatestAnalysis();
                juce::String key = juce::String ((int) moduleOn) + juce::String ((int) r.enoughMaterial) + juce::String (juce::roundToInt (r.voicedSeconds * 2.0f))
                                   + juce::String (juce::roundToInt (r.peakDb)) + juce::String (r.clipEvents) + juce::String ((int) proc.getAutoMix().getReport().valid);
                for (auto& f : r.findings)
                    key << f.title << (int) f.certainty << (int) f.handling << juce::roundToInt (f.confidence * 20.0f);
                if (key != lastKey)
                {
                    lastKey = key;
                    on = moduleOn;
                    content.layout (getWidth() - 10);
                }
            }

        private:
            struct Content final : public juce::Component
            {
                explicit Content (FindingsList& o) : owner (o) {}

                struct Card
                {
                    juce::Rectangle<float> bounds, link;
                    int module = -1;
                    juce::TextLayout body;
                };

                void layout (int width)
                {
                    width = std::max (300, width);
                    cards.clear();
                    const auto& r = owner.proc.getLatestAnalysis();
                    float y = 0.0f;

                    // Facts strip and auto-mix summary
                    factsHeight = owner.on && (r.analysedSeconds > 0.0f || r.peakDb > -99.0f) ? 58.0f : 0.0f;
                    y += factsHeight;
                    const auto& report = owner.proc.getAutoMix().getReport();
                    summary = {};
                    if (owner.on && report.valid && ! report.steps.empty())
                    {
                        juce::AttributedString a;
                        a.append ("What the automatic mix changed\n", font (13.5f, true), colours::text);
                        int n = 0;
                        for (auto& s : report.steps)
                            if (n++ < 6)
                                a.append (juce::String::fromUTF8 ("\xe2\x80\xa2  ") + s.title + "\n", font (13.0f), colours::textDim);
                        summary.createLayout (a, (float) width - 32.0f);
                        summaryBounds = { 0.0f, y, (float) width, summary.getHeight() + 22.0f };
                        y += summaryBounds.getHeight() + 10.0f;
                    }

                    for (auto& f : r.findings)
                    {
                        Card c;
                        juce::AttributedString a;
                        const auto label = [&] (const juce::String& head, const juce::String& text, juce::Colour colour)
                        {
                            if (text.isEmpty())
                                return;
                            a.append (head + "  ", caps (10.5f), colours::textFaint);
                            a.append (text + "\n", font (13.5f), colour);
                        };
                        a.append (f.title + "\n", font (15.5f, true), colours::text);
                        label ("EVIDENCE", f.detail, colours::textDim);
                        label ("WHY IT MATTERS", f.why, colours::textDim);
                        label ("SUGGESTION", f.advice, colours::cyan.interpolatedWith (colours::text, 0.45f));
                        if (f.handlingText.isNotEmpty())
                            label (f.handling == dsp::Finding::Handling::appliedBefore ? "ALREADY BEFORE" : "HANDLED", f.handlingText,
                                   f.handling == dsp::Finding::Handling::appliedBefore ? colours::bypass : colours::ok);
                        else
                            label ("STATUS", f.module.has_value() ? juce::String ("Not applied automatically - your choice.")
                                                                  : juce::String ("Needs a change before the plugin (recording or track level)."), colours::textFaint);
                        c.body.createLayout (a, (float) width - 150.0f);
                        c.bounds = { 0.0f, y, (float) width, std::max (64.0f, c.body.getHeight() + 22.0f) };
                        c.module = f.module.has_value() ? (int) *f.module : -1;
                        if (c.module >= 0)
                            c.link = { c.bounds.getRight() - 130.0f, c.bounds.getY() + 44.0f, 118.0f, 24.0f };
                        y += c.bounds.getHeight() + 8.0f;
                        cards.push_back (std::move (c));
                    }

                    status.clear();
                    if (! owner.on)
                        status = "VOCAL CHECK is off. Switch it on with the power button - it analyses the signal at its place in the chain and never changes the sound.";
                    else if (! r.enoughMaterial)
                        status = "Collecting material: " + juce::String (r.voicedSeconds, 1) + " of " + juce::String (juce::roundToInt (dsp::VocalAnalyzer::minVoicedSeconds))
                                 + " s of voice. Play the vocal - pauses are ignored.";
                    else if (r.findings.empty())
                        status = "Nothing stands out in " + juce::String (r.voicedSeconds, 1) + " s of voice: level, tone, sibilance, dynamics"
                                 + (r.stereo ? ", stereo" : "") + " and noise are inside the usual range.";
                    if (status.isNotEmpty())
                        y += 46.0f;

                    setSize (width, (int) std::ceil (y) + 4);
                    repaint();
                }

                void paint (juce::Graphics& g) override
                {
                    const auto& r = owner.proc.getLatestAnalysis();
                    float y = 0.0f;

                    // Measured facts (no judgement)
                    if (factsHeight > 0.0f)
                    {
                        drawCaption (g, "Measured at this point of the chain", { 0.0f, 0.0f, (float) getWidth(), 16.0f },
                                     juce::Justification::centredLeft, colours::textFaint, 9.5f);
                        juce::StringArray facts;
                        facts.add ("Peak " + (r.peakDb > -99.0f ? juce::String (r.peakDb, 1) + " dBFS" : juce::String ("-")));
                        if (r.loudnessRangeDb > 0.0f)
                            facts.add ("Level range " + juce::String (r.loudnessRangeDb, 1) + " dB");
                        if (r.crestDb > 0.0f)
                            facts.add ("Crest " + juce::String (r.crestDb, 1) + " dB");
                        if (r.sibilanceVsVowelDb > -99.0f)
                            facts.add ("S vs vowels " + juce::String (r.sibilanceVsVowelDb, 1) + " dB");
                        if (r.noiseFloorDb > -199.0f)
                            facts.add ("Noise floor " + juce::String (r.noiseFloorDb, 0) + " dBFS (" + juce::String (r.snrDb, 0) + " dB below voice)");
                        if (r.stereo)
                            facts.add ("Correlation " + juce::String (r.correlation, 2));
                        facts.add ("Voice " + juce::String (r.voicedSeconds, 1) + " s");
                        float x = 0.0f;
                        for (auto& t : facts)
                        {
                            const float w = (float) juce::GlyphArrangement::getStringWidthInt (medium (12.0f), t) + 22.0f;
                            if (x + w > (float) getWidth())
                                break;
                            const auto chip = juce::Rectangle<float> (x, 22.0f, w, 26.0f);
                            g.setColour (colours::accent.withAlpha (0.035f));
                            g.fillRoundedRectangle (chip, 13.0f);
                            g.setColour (colours::lineSoft.brighter (0.3f));
                            g.drawRoundedRectangle (chip.reduced (0.5f), 13.0f, 1.0f);
                            g.setColour (colours::textDim);
                            g.setFont (medium (12.0f));
                            g.drawText (t, chip, juce::Justification::centred, false);
                            x += w + 8.0f;
                        }
                        y = factsHeight;
                    }

                    if (! summaryBounds.isEmpty())
                    {
                        g.setColour (colours::ok.withAlpha (0.05f));
                        g.fillRoundedRectangle (summaryBounds, 10.0f);
                        g.setColour (colours::ok.withAlpha (0.35f));
                        g.drawRoundedRectangle (summaryBounds.reduced (0.5f), 10.0f, 1.0f);
                        summary.draw (g, summaryBounds.reduced (16.0f, 11.0f));
                    }

                    for (size_t i = 0; i < cards.size() && i < r.findings.size(); ++i)
                    {
                        const auto& c = cards[i];
                        const auto& f = r.findings[i];
                        const bool sure = f.certainty == dsp::Finding::Certainty::sure;
                        g.setColour (colours::accent.withAlpha (0.028f));
                        g.fillRoundedRectangle (c.bounds, 10.0f);
                        g.setColour (sure ? colours::cyan.withAlpha (0.28f) : colours::lineSoft.brighter (0.25f));
                        g.drawRoundedRectangle (c.bounds.reduced (0.5f), 10.0f, 1.0f);

                        // Kind and confidence, as text (never colour alone)
                        auto right = c.bounds.withTrimmedLeft (c.bounds.getWidth() - 130.0f).reduced (6.0f, 10.0f);
                        auto badge = right.removeFromTop (22.0f).withSizeKeepingCentre (118.0f, 22.0f);
                        if (sure)
                        {
                            g.setGradientFill (auroraGradient (badge.getTopLeft(), badge.getTopRight(), 0.85f));
                            g.fillRoundedRectangle (badge, 11.0f);
                            drawCaption (g, "Likely problem", badge, juce::Justification::centred, colours::background, 9.0f);
                        }
                        else
                        {
                            g.setColour (colours::line.brighter (0.5f));
                            g.drawRoundedRectangle (badge.reduced (0.5f), 11.0f, 1.0f);
                            drawCaption (g, "Observation", badge, juce::Justification::centred, colours::textDim, 9.0f);
                        }
                        drawCaption (g, "Confidence " + juce::String (juce::roundToInt (f.confidence * 100.0f)) + " %",
                                     right.removeFromTop (16.0f).translated (0.0f, 2.0f), juce::Justification::centred, colours::textFaint, 9.0f);
                        if (c.module >= 0)
                        {
                            g.setColour (colours::cyan.withAlpha (hoveredLink == (int) i ? 0.18f : 0.08f));
                            g.fillRoundedRectangle (c.link, 12.0f);
                            drawCaption (g, juce::String ("Open ") + dsp::moduleDisplayName ((dsp::ModuleId) c.module), c.link,
                                         juce::Justification::centred, colours::cyan, 9.0f);
                        }
                        c.body.draw (g, c.bounds.reduced (16.0f, 11.0f).withTrimmedRight (124.0f));
                        y = c.bounds.getBottom() + 8.0f;
                    }

                    if (status.isNotEmpty())
                    {
                        g.setColour (colours::textDim);
                        g.setFont (font (14.0f));
                        g.drawFittedText (status, juce::Rectangle<float> (0.0f, y + 4.0f, (float) getWidth(), 40.0f).toNearestInt(),
                                          juce::Justification::centredLeft, 2);
                    }
                }

                void mouseMove (const juce::MouseEvent& e) override
                {
                    int over = -1;
                    for (size_t i = 0; i < cards.size(); ++i)
                        if (cards[i].module >= 0 && cards[i].link.contains (e.position))
                            over = (int) i;
                    if (over != hoveredLink)
                    {
                        hoveredLink = over;
                        setMouseCursor (over >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                        repaint();
                    }
                }

                void mouseUp (const juce::MouseEvent& e) override
                {
                    for (auto& c : cards)
                        if (c.module >= 0 && c.link.contains (e.position) && owner.onOpen)
                            owner.onOpen (c.module);
                }

                FindingsList& owner;
                std::vector<Card> cards;
                juce::TextLayout summary;
                juce::Rectangle<float> summaryBounds;
                float factsHeight = 0.0f;
                juce::String status;
                int hoveredLink = -1;
            };

            VelvoxAudioProcessor& proc;
            bool on = false;
            juce::String lastKey;
            juce::Viewport viewport;
            Content content;
        };

        class CheckPanel final : public DetailPanel
        {
        public:
            explicit CheckPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Check", help::shortDescription ((int) ModuleId::check), pid::checkOn),
                  findings (p)
            {
                visualHeight = 150;
                mode = &segments (pid::checkMode);
                addAndMakeVisible (findings);
                addAndMakeVisible (startStop);
                addAndMakeVisible (resetButton);
                addAndMakeVisible (statusLabel);
                statusLabel.setFont (font (13.0f));
                statusLabel.setColour (juce::Label::textColourId, colours::textDim);
                statusLabel.setJustificationType (juce::Justification::topLeft);

                startStop.setTooltip ("Capture mode: analyse everything between Start and Stop");
                startStop.onClick = [this]
                {
                    auto& a = proc.getAnalyzer();
                    if (a.isCapturing()) a.stopCapture(); else a.startCapture();
                    proc.refreshAnalysis();
                };
                resetButton.setTooltip ("Forget everything analysed so far (also the peak / clip memory)");
                resetButton.onClick = [this] { proc.getAnalyzer().clear(); proc.refreshAnalysis(); };
                findings.onOpen = [this] (int id) { if (onOpenModule) onOpenModule (id); };
            }

            void refresh() override
            {
                DetailPanel::refresh();
                auto& a = proc.getAnalyzer();
                const bool on = apvts.getRawParameterValue (pid::checkOn)->load() > 0.5f;
                const bool capture = a.getMode() == dsp::VocalAnalyzer::Mode::capture;
                startStop.setEnabled (on && capture);
                startStop.setButtonText (a.isCapturing() ? "Stop" : "Start");
                startStop.setToggleState (a.isCapturing(), juce::dontSendNotification);
                resetButton.setEnabled (on);

                const auto& r = proc.getLatestAnalysis();
                const juce::String text = ! on ? juce::String ("Off - the audio is not analysed.")
                                        : capture ? (a.isCapturing() ? "Capturing ... " + juce::String (r.analysedSeconds, 1) + " s"
                                                                     : "Capture stopped. " + juce::String (r.analysedSeconds, 1) + " s analysed.")
                                                  : "Continuous: last " + juce::String (juce::roundToInt (std::min (30.0f, r.analysedSeconds))) + " s.";
                if (statusLabel.getText() != text)
                    statusLabel.setText (text, juce::dontSendNotification);
                findings.refresh (on);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto controls = r.removeFromLeft (236);
                r.removeFromLeft (16);
                auto c = controls;
                placeGroup ("Analysis", *mode, c);
                auto buttons = c.removeFromTop (34);
                startStop.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
                buttons.removeFromLeft (8);
                resetButton.setBounds (buttons);
                c.removeFromTop (12);
                statusLabel.setBounds (c.removeFromTop (44));
                findings.setBounds (r);
            }

            Segments* mode;
            FindingsList findings;
            juce::TextButton startStop { "Start" }, resetButton { "Reset" };
            juce::Label statusLabel;
        };
    }

    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int id, const SpectrumSource* spectrum, const LiveData* live)
    {
        std::unique_ptr<DetailPanel> panel;
        DetailPanel::constructionInstance = id < 0 ? 0 : dsp::selectionInstance (id);
        if (id < 0)
        {
            panel = std::make_unique<OutputPanel> (p);
        }
        else
        {
            switch (dsp::selectionModule (id))
            {
                case ModuleId::eq:      panel = std::make_unique<EqPanel> (p); break;
                case ModuleId::comp:    panel = std::make_unique<CompPanel> (p); break;
                case ModuleId::deess:   panel = std::make_unique<DeEssPanel> (p); break;
                case ModuleId::sat:     panel = std::make_unique<SatPanel> (p); break;
                case ModuleId::doubler: panel = std::make_unique<DoublerPanel> (p); break;
                case ModuleId::pitch:   panel = std::make_unique<PitchPanel> (p); break;
                case ModuleId::chop:    panel = std::make_unique<ChopPanel> (p); break;
                case ModuleId::delay:   panel = std::make_unique<DelayPanel> (p); break;
                case ModuleId::reverb:  panel = std::make_unique<ReverbPanel> (p); break;
                case ModuleId::pulse:   panel = std::make_unique<PulsePanel> (p); break;
                case ModuleId::smash:   panel = std::make_unique<SmashPanel> (p); break;
                case ModuleId::choir:   panel = std::make_unique<ChoirPanel> (p); break;
                case ModuleId::smooth:  panel = std::make_unique<SmoothPanel> (p); break;
                case ModuleId::check:   panel = std::make_unique<CheckPanel> (p); break;
                case ModuleId::ride:    panel = std::make_unique<RidePanel> (p); break;
            }
            if (panel == nullptr)
                panel = std::make_unique<OutputPanel> (p);
        }
        DetailPanel::constructionInstance = 0;
        panel->setLiveData (live);
        panel->setVisual (createModuleView (p, id, spectrum));
        panel->setModule (id);
        return panel;
    }
}
