#pragma once

#include <juce_core/juce_core.h>
#include "../DSP/Routing.h"
#include "../Text/I18n.h"

// Offline help: every page is compiled into the plugin (no files, no web), in German and English.
// Page text uses a tiny markup: "## " starts a heading, "- " a bullet,
// blank lines separate paragraphs.
namespace velvox::ui::help
{
    struct Page
    {
        juce::String key;     // "start", "chain", ... or a module key ("eq", "ride", ...) or "output"
        juce::String title;
        juce::String group;   // navigation group
        juce::String body;
    };

    // All pages in navigation order, in the current interface language (or a given one). Each
    // language's table is built once and kept, so Page pointers stay valid after a language switch.
    const std::vector<Page>& pages();
    const std::vector<Page>& pages (i18n::Language);
    const Page* find (const juce::String& key);

    // Page key for a module tile / detail panel (moduleId < 0 = output stage)
    juce::String keyForModule (int moduleIdOrOutput);

    // One-line description (module panel header, Beginner focus line), current language; id < 0 = output
    juce::String shortDescription (int moduleIdOrOutput);

    // The seven headings every module page answers, in this order (current language / a given one)
    const juce::StringArray& moduleSectionHeadings();
    const juce::StringArray& moduleSectionHeadings (i18n::Language);
}
