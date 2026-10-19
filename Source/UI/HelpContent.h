#pragma once

#include <juce_core/juce_core.h>
#include "../DSP/Routing.h"

// Offline help: every page is compiled into the plugin (no files, no web).
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

    // All pages in navigation order
    const std::vector<Page>& pages();
    const Page* find (const juce::String& key);

    // Page key for a module tile / detail panel (moduleId < 0 = output stage)
    juce::String keyForModule (int moduleIdOrOutput);

    // One-line description (module panel header, Beginner focus line); id < 0 = output
    juce::String shortDescription (int moduleIdOrOutput);

    // The seven headings every module page answers, in this order
    const juce::StringArray& moduleSectionHeadings();
}
